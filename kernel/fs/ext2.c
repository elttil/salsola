#include <assert.h>
#include <error.h>
#include <fs/ext2.h>
#include <fs/vfs.h>
#include <kmalloc.h>
#include <log.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>

#define SECTOR_SIZE 512

#define EXT2_SUPERBLOCK_SECTOR 2
#define EXT2_ROOT_INODE 2

#define BLOCKS_REQUIRED(_a, _b) ((_a) / (_b) + (((_a) % (_b)) != 0))

#define ALIGN(value, alignment)                                                \
  if (0 != (value % alignment)) {                                              \
    value += (alignment - (value % alignment));                                \
  }

struct ext2_ctx {
  struct vfs_fd *fd;
  union {
    struct ExtendedSuperblock superblock;
    char padding[2 * SECTOR_SIZE];
  };
  u32 block_byte_size;
  u32 inode_size;
  u32 inodes_per_block;
  bool superblock_changed;
};

static void read_block(struct ext2_ctx *ctx, u32 block, void *address,
                       size_t size, size_t offset);

static void get_group_descriptor(struct ext2_ctx *ctx, u32 group_index,
                                 bgdt_t *block_group) {
  int starting_block = (1024 == ctx->block_byte_size) ? 2 : 1;
  read_block(ctx, starting_block, block_group, sizeof(bgdt_t),
             group_index * sizeof(bgdt_t));
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
  u8 block[ctx->block_byte_size];
  read_block(ctx, singly_block_ptr, block, ctx->block_byte_size, 0);
  u32 index = *(u32 *)(block + (i * (32 / 8)));
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

static void read_block(struct ext2_ctx *ctx, u32 block, void *address,
                       size_t size, size_t offset) {
  // TODO: Cache
  int err;
  vfs_pread(ctx->fd, address, size, block * ctx->block_byte_size + offset,
            &err);
  assert(err == ERROR_SUCCESS);
}

static void get_inode_header(struct ext2_ctx *ctx, u32 inode_index, u8 *data) {
  memset(data + sizeof(inode_t), 0, ctx->inode_size - sizeof(inode_t));
  u32 block_index;
  u32 block_offset;
  get_block_containing_inode(ctx, inode_index, &block_index, &block_offset);

  u8 mem_block[ctx->inode_size];
  read_block(ctx, block_index, mem_block, ctx->inode_size, block_offset);

  memcpy(data, mem_block, ctx->inode_size);
}

static size_t read_inode(struct ext2_ctx *ctx, u32 inode_num, u8 *data,
                         u64 size, u64 offset, u64 *file_size) {
  // TODO: Fail if size is lower than the size of the file being read, and
  //       return the size of the file the callers is trying to read.
  u8 inode_buffer[ctx->inode_size];
  get_inode_header(ctx, inode_num, inode_buffer);
  inode_t *inode = (inode_t *)inode_buffer;

  u64 fsize = (u64)(((u64)inode->_upper_32size << 32) | (u64)inode->low_32size);

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

    u32 block = get_block(ctx, inode, i);
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
  (void)read_inode(ctx, directory_inode, NULL, 0, 0, &file_size);
  u64 allocation_size = file_size;
  u8 data[allocation_size];
  (void)read_inode(ctx, directory_inode, data, allocation_size, 0, NULL);

  direntry_header_t *dir;
  u8 *data_p = data;
  u8 *data_end = data + allocation_size;
  for (; data_p <= (data_end - sizeof(direntry_header_t)) &&
         (dir = (direntry_header_t *)data_p)->inode;
       data_p += dir->size) {
    if (0 == dir->size) {
      break;
    }
    if (0 == dir->name_length) {
      continue;
    }
    if (sv_length(file) != dir->name_length) {
      continue;
    }

    assert(data_p + sizeof(direntry_header_t) + dir->name_length <= data_end);
    if (0 == memcmp(data_p + sizeof(direntry_header_t), sv_buffer(file),
                    dir->name_length)) {
      if (entry) {
        memcpy(entry, data_p, sizeof(direntry_header_t));
      }
      if (inode) {
        *inode = dir->inode;
      }
      return true;
    }
  }
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

    // The expected returned entry is a directory
    if (TYPE_INDICATOR_DIRECTORY != a.type_indicator) {
      // klog(LOG_WARN, "ext2: Expected diretory but got: %d",
      // a.type_indicator);
      return false;
    }
  }
  if (inode) {
    *inode = cur_path_inode;
  }
  return true;
}

size_t ext2_read(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                 err_t *err) {
  ASSIGN_PTR(err, ERROR_SUCCESS);
  struct ext2_ctx *ctx = (struct ext2_ctx *)fd->mount->internal_object;
  u32 inode_num = (u32)fd->internal_object;
  return read_inode(ctx, inode_num, buffer, length, offset, NULL);
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

  struct vfs_fd *fd = kcalloc(1, sizeof(struct vfs_fd));
  if (!fd) {
    ASSIGN_PTR(err, ERROR_NO_MEMORY);
    return NULL;
  }
  fd->close = NULL;
  fd->offset = 0;
  fd->read = ext2_read;
  fd->write = NULL;
  fd->lseek = ext2_lseek;
  fd->type = VFS_TYPE_FILE;
  fd->internal_object = (void *)inode_num;

  //  assert(p->open);
  //  assert(true == p->open(fd, file, flags, p->internal_object, err));

  return fd;
}

static bool parse_superblock(struct ext2_ctx *ctx, err_t *rc_err) {
  err_t err;
  vfs_pread(ctx->fd, &ctx->superblock, 2 * SECTOR_SIZE,
            EXT2_SUPERBLOCK_SECTOR * SECTOR_SIZE, &err);
  if (ERROR_SUCCESS != err) {
    ASSIGN_PTR(rc_err, err);
    return false;
  }

  ctx->block_byte_size = 1024 << ctx->superblock.block_size;

  if (0xEF53 != ctx->superblock.ext2_signature) {
    klog(LOG_ERROR, "Incorrect ext2 signature in superblock.");
    ASSIGN_PTR(rc_err, ERROR_INVALID_FORMAT);
    return false;
  }

  if (1 <= ctx->superblock.major_version) {
    ctx->inode_size = ctx->superblock.inode_size;
  }

  ctx->inodes_per_block = ctx->block_byte_size / ctx->inode_size;
  //  cache = kcalloc(num_block_cache, sizeof(struct block_cache));
  return true;
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
  ctx->fd = fd;
  ctx->superblock_changed = false;

  mount->open = ext2_open;
  mount->internal_object = (void *)ctx;

  if (!parse_superblock(ctx, NULL)) {
    kfree(ctx);
    kfree(mount);
    return NULL;
  }

  return mount;
}
