#include <assert.h>
#include <buffer.h>
#include <drivers/framebuffer.h>
#include <fs/ramfs.h>
#include <fs/vfs.h>
#include <kprintf.h>
#include <mmu.h>
#include <fonts.h>

#include <prng.h>

struct display_info {
  u8 *framebuffer;
  u32 framebuffer_physical;
  u32 framebuffer_width;
  u32 framebuffer_height;
  u64 framebuffer_size;
  struct buffer buffer;

  u32 width;
  u32 height;
  u8 bpp;
};

struct display_info vbe_info;

#define place_pixel_pos(_p, _pos)                                              \
  { *(u32 *)((u32 *)vbe_info.framebuffer + _pos) = _p; }

void framebuffer_clear_screen(u32 color) {
  u32 *p = (u32 *)vbe_info.framebuffer;
  for (u32 i = 0; i < vbe_info.framebuffer_width * vbe_info.framebuffer_height; i++, p++) {
    *p = color;
  }
}

static int get_bitmap_value(const unsigned char bitmap[], int i) {
  int array_index = i / 8;
  int byte_index = i % 8;
  int rc = (bitmap[array_index] >> byte_index) & 0x1;
  return rc;
}

void framebuffer_drawfont(u32 px, u32 py, const u8 c) {
  u32 x, y;
  x = px;
  y = py;
  if (px + 8 > vbe_info.framebuffer_width) {
    return;
  }
  if (py + 8 > vbe_info.framebuffer_height) {
    return;
  }
  const unsigned char *bitmap = font8x8_basic[c];
  for (int i = 0; i < 8 * 8; i++) {
    u32 pos = x + y * vbe_info.framebuffer_width;
    if (get_bitmap_value(bitmap, i)) {
      place_pixel_pos(0xFFFFFF, pos);
    }
    x++;
    if (x >= 8 + px) {
      y++;
      x = px;
    }
    if (y > py + 8) {
      break;
    }
  }
}

err_t framebuffer_write(struct vfs_fd *fd, const void *buffer, size_t length,
                        size_t offset, size_t *rc) {
  struct display_info *info = fd->internal_object;

  return buffer_write(&info->buffer, buffer, length, offset, rc);
}

err_t framebuffer_read(struct vfs_fd *fd, void *buffer, size_t length,
                       size_t offset, size_t *rc) {
  struct display_info *info = fd->internal_object;

  return buffer_read(&info->buffer, buffer, length, offset, rc);
}

err_t framebuffer_mmap(struct vfs_fd *fd, void *addr, size_t length, int prot,
                       int flags, size_t offset, void **out) {
  // TODO: Offset
  (void)offset;
  (void)flags;
  (void)prot;
  struct display_info *info = fd->internal_object;

  void *to_allocate = (void *)info->framebuffer_physical;
  size_t allocation_size = length;

  void *r;
  TRY(mmu_setup_random_region(addr, length, true, false, 0, &r));
  r = mmu_map_frames_to_region(to_allocate, allocation_size, r,
                               MMU_FLAG_RW | MMU_FLAG_USER | MMU_FLAG_SHARED);
  ASSIGN_PTR(out, r);
  return ERROR_SUCCESS;
}

bool framebuffer_open(struct vfs_fd *fd, struct sv file, int flags,
                      void *internal_object, int *err) {
  (void)file;
  (void)flags;
  (void)err;
  fd->internal_object = internal_object;
  fd->type = VFS_TYPE_BLOCK_DEVICE;
  fd->read = framebuffer_read;
  fd->write = framebuffer_write;
  fd->mmap = framebuffer_mmap;
  return true;
}

bool framebuffer_add_device(struct sv filename) {
  struct vfs_mount *mount = vfs_find_mount(C_TO_SV("/dev"));
  assert(mount);
  assert(ramfs_add_file(mount, filename, framebuffer_open, &vbe_info, NULL));
  return true;
}

bool display_driver_init(struct multiboot_tag_framebuffer_common *mbi) {
  vbe_info.framebuffer_width = mbi->framebuffer_width;
  vbe_info.framebuffer_height = mbi->framebuffer_height;

  u32 bits_pp = mbi->framebuffer_bpp;
  u32 bytes_pp = (bits_pp / 8) + (8 - (bits_pp % 8));

  vbe_info.framebuffer_size =
      bytes_pp * vbe_info.framebuffer_width * vbe_info.framebuffer_height;

  vbe_info.framebuffer_physical = mbi->framebuffer_addr;
  vbe_info.framebuffer =
      mmu_map_frames((void *)mbi->framebuffer_addr, vbe_info.framebuffer_size,
                     MMU_FLAG_RW | MMU_FLAG_PCD | MMU_FLAG_PRESENT);
  if (!vbe_info.framebuffer) {
    return false;
  }

  vbe_info.width = vbe_info.framebuffer_width;
  vbe_info.height = vbe_info.framebuffer_height;
  vbe_info.bpp = mbi->framebuffer_bpp;

  buffer_init(&vbe_info.buffer, vbe_info.framebuffer,
              vbe_info.framebuffer_size);

  return true;
}
