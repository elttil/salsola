#include <arch/amd64/msr.h>
#include <assert.h>
#include <drivers/ahci.h>
#include <error.h>
#include <fs/ext2.h>
#include <fs/vfs.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <lock.h>
#include <log.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>

#define EXT2_SUPERBLOCK_SECTOR 2
#define EXT2_ROOT_INODE 2

#define EXT2_NUM_CACHE 1024

#define BLOCKS_REQUIRED(_a, _b) ((_a) / (_b) + (((_a) % (_b)) != 0))

#define INDIRECT_BLOCK_CAPACITY (ctx->block_byte_size / sizeof(u32))

#define ALIGN(value, alignment)                                                \
  if (0 != (value % alignment)) {                                              \
    value += (alignment - (value % alignment));                                \
  }

struct ext2_block_cache {
  void *buffer;
  u64 last_usage;
  // TODO: Reduce struct size
  u32 block_number;
  bool in_use;
};

struct ext2_ctx {
  lock_t lock_ctx;
  struct vfs_fd *fd;
  union {
    struct ExtendedSuperblock superblock;
    char padding[2 * SECTOR_SIZE];
  };
  u32 block_byte_size;
  u32 inode_size;
  u32 inodes_per_block;
  bool superblock_changed;
  lock_t lock_pre_allocated_block;
  void *pre_allocated_block;
  // TODO: Maybe allocate this differently
  lock_t lock_cache;
  struct ext2_block_cache cache[EXT2_NUM_CACHE];
};

static void read_block(struct ext2_ctx *ctx, u32 block, void *address,
                       size_t size, size_t offset);
static void write_block(struct ext2_ctx *ctx, u32 block, const void *address,
                        size_t size, size_t offset);
static int get_free_block(struct ext2_ctx *ctx, bool allocate);

static void *get_allocated_block(struct ext2_ctx *ctx) {
  return ctx->pre_allocated_block;
}

static u32 num_block_groups(struct ext2_ctx *ctx) {
  ext_superblock_t *superblock = &ctx->superblock;
  // Determining the Number of Block Groups

  // From the Superblock, extract the size of each block, the total
  // number of inodes, the total number of blocks, the number of blocks
  // per block group, and the number of inodes in each block group. From
  // this information we can infer the number of block groups there are
  // by:

  // Rounding up the total number of blocks divided by the number of
  // blocks per block group
  u32 num_blocks = superblock->num_blocks;
  u32 num_blocks_in_group = superblock->num_blocks_group;
  u32 b = num_blocks / num_blocks_in_group;
  // TODO: This calculation seems to generate a off by one error
  // for certain ext2 filesystems, it is better to be off by one than
  // one too many in this case so we risk leaving a group unused.
  //  if (num_blocks % num_blocks_in_group != 0) {
  //    b++;
  //  }

  // Rounding up the total number of inodes divided by the number of
  // inodes per block group
  u32 num_inodes = superblock->num_inodes;
  u32 num_inodes_in_group = superblock->num_inodes_group;
  u32 i = num_inodes / num_inodes_in_group;
  // See TODO above.
  //  if (num_inodes % num_inodes_in_group != 0) {
  //    i++;
  //  }
  // Both (and check them against each other)
  assert(i == b);
  return i;
}

static void write_group_descriptor(struct ext2_ctx *ctx, u32 group_index,
                                   bgdt_t *block_group) {
  int starting_block = (1024 == ctx->block_byte_size) ? 2 : 1;
  write_block(ctx, starting_block, (u8 *)block_group, sizeof(bgdt_t),
              group_index * sizeof(bgdt_t));
}

static void get_group_descriptor(struct ext2_ctx *ctx, u32 group_index,
                                 bgdt_t *block_group) {
  u32 starting_block = (1024 == ctx->block_byte_size) ? 2 : 1;

  u32 block_index = (group_index * sizeof(bgdt_t)) / ctx->block_byte_size;
  u32 block_offset = (group_index * sizeof(bgdt_t)) % ctx->block_byte_size;
  read_block(ctx, starting_block + block_index, block_group, sizeof(bgdt_t),
             block_offset);
}

static void write_to_indirect_block(struct ext2_ctx *ctx, u32 indirect_block,
                                    u32 index, u32 new_block) {
  index %= INDIRECT_BLOCK_CAPACITY;
  write_block(ctx, indirect_block, (u8 *)&new_block, sizeof(u32),
              index * sizeof(u32));
}

static void get_block_containing_inode(struct ext2_ctx *ctx, u32 inode_index,
                                       u32 *block_index, u32 *offset) {
  bgdt_t block_group;
  get_group_descriptor(
      ctx, (inode_index - 1) / ctx->superblock.num_inodes_group, &block_group);

  u64 full_offset =
      ((inode_index - 1) % ctx->superblock.num_inodes_group) * ctx->inode_size;
  *block_index = block_group.starting_inode_table +
                 (full_offset >> (ctx->superblock.block_size + 10));
  *offset = full_offset & (ctx->block_byte_size - 1);
}

static u32 get_singly_block_index(struct ext2_ctx *ctx, u32 singly_block_ptr,
                                  u32 i) {
  u32 index;
  read_block(ctx, singly_block_ptr, &index, sizeof(index), (i * (32 / 8)));
  return index;
}

static int get_block(struct ext2_ctx *ctx, inode_t *inode, u32 i) {
  if (i < 12) {
    return inode->block_pointers[i];
  }

  i -= 12;
  u32 singly_block_byte_size = ctx->block_byte_size / (32 / 8);
  u32 double_block_byte_size =
      (singly_block_byte_size * singly_block_byte_size);
  if (i < singly_block_byte_size) {
    return get_singly_block_index(ctx, inode->single_indirect_block_pointer, i);
  } else if (i < double_block_byte_size) {
    i -= singly_block_byte_size;
    u32 singly_entry = get_singly_block_index(
        ctx, inode->double_indirect_block_pointer, i / singly_block_byte_size);
    u32 offset_in_entry = i % singly_block_byte_size;
    int block = get_singly_block_index(ctx, singly_entry, offset_in_entry);
    return block;
  }
  assert(0);
  return 0;
}

static int allocate_block(struct ext2_ctx *ctx, inode_t *inode, u32 index,
                          int block) {
  if (index < 12) {
    inode->block_pointers[index] = block;
    return 1;
  }
  index -= 12;
  if (index < INDIRECT_BLOCK_CAPACITY) {
    if (0 == index) {
      int n = get_free_block(ctx, true);
      if (-1 == n) {
        return 0;
      }
      inode->single_indirect_block_pointer = n;
    }
    write_to_indirect_block(ctx, inode->single_indirect_block_pointer, index,
                            block);
    return 1;
  }
  index -= INDIRECT_BLOCK_CAPACITY;
  if (index < INDIRECT_BLOCK_CAPACITY * INDIRECT_BLOCK_CAPACITY) {
    if (0 == index) {
      int n = get_free_block(ctx, true);
      if (-1 == n) {
        return 0;
      }
      inode->double_indirect_block_pointer = n;
    }

    u32 value;
    if (0 == (index % INDIRECT_BLOCK_CAPACITY)) {
      int n = get_free_block(ctx, true);
      if (-1 == n) {
        return 0;
      }
      write_to_indirect_block(ctx, inode->double_indirect_block_pointer,
                              index / INDIRECT_BLOCK_CAPACITY, n);
      value = n;
    } else {
      value = get_singly_block_index(ctx, inode->double_indirect_block_pointer,
                                     index / INDIRECT_BLOCK_CAPACITY);
    }

    write_to_indirect_block(ctx, value, index, block);
    return 1;
  }
  return 0;
}

static void read_block(struct ext2_ctx *ctx, u32 block, void *address,
                       size_t size, size_t offset) {
  lock_acquire(&ctx->lock_cache);
  bool reuse_cache = false;
read_block_redo:
  int index = -1;
  u64 min_last_usage = ~((u64)0);
  for (int i = 0; i < EXT2_NUM_CACHE; i++) {
    if (!ctx->cache[i].in_use && !reuse_cache) {
      min_last_usage = 0;
      index = i;
      break;
    }
    if (block == ctx->cache[i].block_number) {
      index = i;
      break;
    }
    if (ctx->cache[i].last_usage < min_last_usage) {
      index = i;
      min_last_usage = ctx->cache[i].last_usage;
    }
  }

  if (reuse_cache) {
    assert(-1 != index);
  }

  struct ext2_block_cache *cache = &ctx->cache[index];

  bool is_new = false;
  if (!cache->in_use) {
    cache->buffer = kmalloc(ctx->block_byte_size);
    if (!cache->buffer) {
      reuse_cache = true;
      goto read_block_redo;
    }
    cache->in_use = true;
    is_new = true;
  }

  if (is_new || block != cache->block_number) {
    assert(ERROR_SUCCESS == vfs_pread(ctx->fd, cache->buffer,
                                      ctx->block_byte_size,
                                      block * ctx->block_byte_size, NULL));
    cache->block_number = block;
  }

  assert(size + offset <= ctx->block_byte_size);
  memcpy(address, (u8 *)cache->buffer + offset, size);

  cache->last_usage = rdtsc();

  lock_release(&ctx->lock_cache);
}

static void write_block(struct ext2_ctx *ctx, u32 block, const void *address,
                        size_t size, size_t offset) {
  // TODO: Cache
  assert(ERROR_SUCCESS == vfs_pwrite(ctx->fd, address, size,
                                     block * ctx->block_byte_size + offset,
                                     NULL));
}

// static void get_inode_header(struct ext2_ctx *ctx, u32 inode_index, u8 *data)
// {
static void get_inode_header(struct ext2_ctx *ctx, u32 inode_index,
                             inode_t *inode) {
  u32 block_index;
  u32 block_offset;
  get_block_containing_inode(ctx, inode_index, &block_index, &block_offset);

  read_block(ctx, block_index, inode, sizeof(inode_t), block_offset);
}

static size_t read_inode(struct ext2_ctx *ctx, u32 inode_num, u8 *data,
                         u64 size, u64 offset, u64 *file_size) {
  // TODO: Fail if size is lower than the size of the file being read, and
  //       return the size of the file the callers is trying to read.
  inode_t inode;
  get_inode_header(ctx, inode_num, &inode);

  u64 fsize = (u64)(((u64)inode._upper_32size << 32) | (u64)inode.low_32size);

  if (file_size) {
    *file_size = fsize;
  }

  if (size > fsize - offset) {
    size -= ((size + offset) - fsize);
  }

  if (size == 0) {
    return 0;
  }

  if (offset > fsize) {
    return 0;
  }

  u32 block_start = offset / ctx->block_byte_size;
  u32 block_offset = offset % ctx->block_byte_size;

  size_t bytes_read = 0;
  for (int i = block_start; size; i++) {
    int read_len = ((size + block_offset) > ctx->block_byte_size)
                       ? (ctx->block_byte_size - block_offset)
                       : size;

    u32 block = get_block(ctx, &inode, i);
    if (0 == block) {
      memset(data + bytes_read, 0, read_len);
    } else {
      read_block(ctx, block, data + bytes_read, read_len, block_offset);
    }

    block_offset = 0;
    bytes_read += read_len;
    size -= read_len;
  }
  return bytes_read;
}

static bool find_inode_in_directory(struct ext2_ctx *ctx, u32 directory_inode,
                                    struct sv file, u32 *inode,
                                    direntry_header_t *entry) {
  if (sv_isempty(file)) {
    if (inode) {
      *inode = directory_inode;
    }
    return true;
  }

  u64 file_size;
  lock_acquire(&ctx->lock_pre_allocated_block);
  u8 *block = get_allocated_block(ctx);
  (void)read_inode(ctx, directory_inode, block, ctx->block_byte_size, 0,
                   &file_size);

  direntry_header_t *dir;

  uintptr_t ptr = 0;
  uintptr_t block_offset = 0;

  //  u8 *data_p = data;
  //  u8 *data_end = data + allocation_size;
  //  for (; data_p <= (data_end - sizeof(direntry_header_t)) &&
  //         (dir = (direntry_header_t *)data_p)->inode;
  for (; ptr <= file_size - sizeof(direntry_header_t);
       ptr += dir->size, block_offset += dir->size) {

    if (block_offset >= ctx->block_byte_size) {
      block_offset %= ctx->block_byte_size;
      (void)read_inode(ctx, directory_inode, block, ctx->block_byte_size,
                       ptr - block_offset, NULL);
    }
    u8 *data_p = block + block_offset;

    dir = (direntry_header_t *)data_p;
    if (0 == dir->inode || 0 == dir->size) {
      break;
    }
    if (0 == dir->name_length) {
      continue;
    }
    if (sv_length(file) != dir->name_length) {
      continue;
    }

    assert(ptr + sizeof(direntry_header_t) + dir->name_length <=
           file_size - sizeof(direntry_header_t));
    if (0 == memcmp(data_p + sizeof(direntry_header_t), sv_buffer(file),
                    dir->name_length)) {
      if (entry) {
        memcpy(entry, data_p, sizeof(direntry_header_t));
      }
      if (inode) {
        *inode = dir->inode;
      }
      lock_release(&ctx->lock_pre_allocated_block);
      return true;
    }
  }
  lock_release(&ctx->lock_pre_allocated_block);
  return false;
}

static bool find_inode(struct ext2_ctx *ctx, struct sv file, u32 *inode,
                       u32 *parent_directory) {
  int cur_path_inode = EXT2_ROOT_INODE;

  if (sv_eq(file, C_TO_SV("/"))) {
    if (parent_directory) {
      *parent_directory = EXT2_ROOT_INODE;
    }
    if (inode) {
      *inode = cur_path_inode;
    }
    return true;
  }

  sv_try_eat(file, &file, C_TO_SV("/"));
  for (;;) {
    int final = 0;
    struct sv start = sv_split_delim(file, &file, '/');
    if (sv_isempty(file)) {
      final = 1;
    }

    if (parent_directory) {
      *parent_directory = cur_path_inode;
    }

    direntry_header_t a;

    u32 new_inode;
    if (!find_inode_in_directory(ctx, cur_path_inode, start, &new_inode, &a)) {
      return false;
    }
    cur_path_inode = new_inode;

    if (final) {
      break;
    }

    if (TYPE_INDICATOR_DIRECTORY != a.type_indicator) {
      return false;
    }
  }
  if (inode) {
    *inode = cur_path_inode;
  }
  return true;
}

static int get_free_blocks(struct ext2_ctx *ctx, bool allocate, int entries[],
                           u32 num_entries) {
  u32 current_entry = 0;
  bgdt_t block_group;
  lock_acquire(&ctx->lock_ctx);
  if (num_entries > ctx->superblock.num_blocks_unallocated) {
    lock_release(&ctx->lock_ctx);
    return 0;
  }
  assert(0 == ctx->superblock.num_blocks_group % 8);
  assert(((ctx->superblock.num_blocks_group) / 8) <= ctx->block_byte_size);
  u8 *bitmap = get_allocated_block(ctx);
  for (u32 group = 0;
       group < num_block_groups(ctx) && current_entry < num_entries; group++) {
    get_group_descriptor(ctx, group, &block_group);

    if (0 == block_group.num_unallocated_blocks_in_group) {
      continue;
    }

    read_block(ctx, block_group.block_usage_bitmap, bitmap,
               (ctx->superblock.num_blocks_group) / 8, 0);
    int found_block = 0;
    for (u32 index = 0; index < ctx->superblock.num_blocks_group / 8 &&
                        current_entry < num_entries;
         index++) {
      if (0xFF == bitmap[index]) {
        continue;
      }
      for (u32 offset = 0; offset < 8 && current_entry < num_entries;
           offset++) {
        if (bitmap[index] & (1 << offset)) {
          continue;
        }
        u32 block_index =
            index * 8 + offset + group * ctx->superblock.num_blocks_group;
        bitmap[index] |= (1 << offset);
        entries[current_entry] = block_index;
        current_entry++;
        found_block = 1;
      }
    }
    if (allocate && found_block) {
      write_block(ctx, block_group.block_usage_bitmap, bitmap,
                  ctx->superblock.num_blocks_group / 8, 0);
      block_group.num_unallocated_blocks_in_group--;
      write_group_descriptor(ctx, group, &block_group);
      ctx->superblock.num_blocks_unallocated--;
      ctx->superblock_changed = 1;
    }
  }
  lock_release(&ctx->lock_ctx);
  return current_entry;
}

static int get_free_block(struct ext2_ctx *ctx, bool allocate) {
  int entry[1];
  if (0 == get_free_blocks(ctx, allocate, entry, 1)) {
    return -1;
  }
  return entry[0];
}

static void write_inode_header(struct ext2_ctx *ctx, int inode_index,
                               inode_t *data) {
  u32 block_index;
  u32 block_offset;
  get_block_containing_inode(ctx, inode_index, &block_index, &block_offset);

  size_t amount = 0;
  size_t left = ctx->inode_size;
  write_block(ctx, block_index, data, sizeof(inode_t), block_offset);
  amount += sizeof(inode_t);
  left -= sizeof(inode_t);

  // NOTE: I hate this
  u8 zero[128];
  memset(zero, 0, sizeof(zero));
  for (; left > 0;) {
    size_t amt = min(sizeof(zero), left);
    write_block(ctx, block_index, zero, amt, block_offset + amount);
    amount += amt;
    left -= amt;
  }
}

static int write_inode(struct ext2_ctx *ctx, int inode_num, const void *data,
                       u64 size, u64 offset, u64 *file_size, int append) {
  (void)file_size;
  inode_t inode;
  get_inode_header(ctx, inode_num, &inode);

  u64 fsize = (u64)(((u64)inode._upper_32size << 32) | (u64)inode.low_32size);
  if (append) {
    offset = fsize;
  }

  u32 block_start = offset / ctx->block_byte_size;
  u32 block_offset = offset % ctx->block_byte_size;

  int num_blocks_used =
      inode.num_disk_sectors / (ctx->block_byte_size / SECTOR_SIZE);

  if (size + offset > fsize) {
    fsize = size + offset;
  }

  u32 num_blocks_required = BLOCKS_REQUIRED(fsize, ctx->block_byte_size);

  u32 delta = num_blocks_required - num_blocks_used;
  if (delta > 0) {
    u32 left = delta;
    u32 written = 0;
    for (; left > 0;) {
      int blocks[32];
      u32 amt = min(32, left);
      get_free_blocks(ctx, true, blocks, amt);
      for (u32 i = 0; i < amt; i++) {
        assert(allocate_block(ctx, &inode, num_blocks_used + written + i,
                              blocks[i]));
      }
      left -= amt;
      written += amt;
    }
  }

  inode.num_disk_sectors =
      num_blocks_required * (ctx->block_byte_size / SECTOR_SIZE);

  int bytes_written = 0;
  for (int i = block_start; size; i++) {
    u32 block = get_block(ctx, &inode, i);
    if (0 == block) {
      break;
    }

    int write_len = ((size + block_offset) > ctx->block_byte_size)
                        ? (ctx->block_byte_size - block_offset)
                        : size;
    write_block(ctx, block, data + bytes_written, write_len, block_offset);
    block_offset = 0;
    bytes_written += write_len;
    size -= write_len;
  }
  inode.low_32size = fsize;
  inode._upper_32size = (fsize >> 32);
  write_inode_header(ctx, inode_num, &inode);
  return bytes_written;
}

err_t ext2_write(struct vfs_fd *fd, const void *buffer, size_t length,
                 size_t offset, size_t *rc) {
  u32 inode_num = (u32)fd->internal_object;
  /* TODO
  assert(fd->inode->type != FS_TYPE_DIRECTORY);
  if (fd->inode->type == FS_TYPE_LINK) {
    inode_num = resolve_link(inode_num);
  }
  */
  struct ext2_ctx *ctx = (struct ext2_ctx *)fd->mount->internal_object;
  ASSIGN_PTR(rc, write_inode(ctx, inode_num, buffer, length, offset, NULL, 0));
  return ERROR_SUCCESS;
}

err_t ext2_read(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                size_t *rc) {
  struct ext2_ctx *ctx = (struct ext2_ctx *)fd->mount->internal_object;
  u32 inode_num = (u32)fd->internal_object;
  ASSIGN_PTR(rc, read_inode(ctx, inode_num, buffer, length, offset, NULL));
  return ERROR_SUCCESS;
}

err_t ext2_lseek(struct vfs_fd *fd, off_t offset, int whence, off_t *out) {
  off_t ret_offset = fd->offset;
  switch (whence) {
  case SEEK_SET:
    ret_offset = offset;
    break;
  case SEEK_CUR:
    ret_offset += offset;
    break;
  case SEEK_END:
    // TODO: Get file size and put that.
    return ERROR_INVALID_WHENCE;
    break;
  default:
    return ERROR_INVALID_WHENCE;
    break;
  }
  fd->offset = ret_offset;
  ASSIGN_PTR(out, ret_offset);
  return ERROR_SUCCESS;
}

struct vfs_fd *ext2_open(struct vfs_mount *mount, struct sv file, int flags,
                         err_t *err) {
  (void)flags;
  struct ext2_ctx *ctx = (struct ext2_ctx *)mount->internal_object;

  u32 inode_num;
  if (!find_inode(ctx, file, &inode_num, NULL)) {
    ASSIGN_PTR(err, ERROR_NO_FILE);
    return NULL;
  }

  struct vfs_fd *fd = vfs_allocate_fd();
  if (!fd) {
    ASSIGN_PTR(err, ERROR_NO_MEMORY);
    return NULL;
  }
  fd->read = ext2_read;
  fd->write = ext2_write;
  fd->lseek = ext2_lseek;
  fd->type = VFS_TYPE_FILE;
  fd->internal_object = (void *)inode_num;

  return fd;
}

static err_t parse_superblock(struct ext2_ctx *ctx) {
  TRY(vfs_pread(ctx->fd, &ctx->superblock, 2 * SECTOR_SIZE,
                EXT2_SUPERBLOCK_SECTOR * SECTOR_SIZE, NULL));

  ctx->block_byte_size = 1024 << ctx->superblock.block_size;

  if (0xEF53 != ctx->superblock.ext2_signature) {
    klog(LOG_ERROR, "Incorrect ext2 signature in superblock.");
    return ERROR_INVALID_FORMAT;
  }

  if (1 <= ctx->superblock.major_version) {
    ctx->inode_size = ctx->superblock.inode_size;
  }

  ctx->inodes_per_block = ctx->block_byte_size / ctx->inode_size;
  return ERROR_SUCCESS;
}

struct vfs_mount *ext2_create(struct vfs_fd *fd) {
  struct vfs_mount *mount = kmalloc(sizeof(struct vfs_mount));
  if (!mount) {
    return NULL;
  }
  struct ext2_ctx *ctx = kmalloc(sizeof(struct ext2_ctx));
  if (!ctx) {
    kfree(mount);
    return NULL;
  }
  lock_release(&ctx->lock_ctx);
  lock_release(&ctx->lock_cache);
  lock_release(&ctx->lock_pre_allocated_block);

  ctx->fd = fd;
  ctx->superblock_changed = false;
  for (int i = 0; i < EXT2_NUM_CACHE; i++) {
    ctx->cache[i].in_use = false;
  }

  mount->open = ext2_open;
  mount->internal_object = (void *)ctx;

  if (ERROR_SUCCESS != parse_superblock(ctx)) {
    kfree(ctx);
    kfree(mount);
    return NULL;
  }

  ctx->pre_allocated_block = kmalloc(ctx->block_byte_size);
  if (!ctx->pre_allocated_block) {
    kfree(ctx);
    kfree(mount);
    return NULL;
  }

  return mount;
}
