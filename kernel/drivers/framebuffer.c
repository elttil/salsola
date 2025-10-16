#include <assert.h>
#include <drivers/framebuffer.h>
#include <kprintf.h>
#include <mmu.h>

u8 *framebuffer;
u32 framebuffer_physical;
u32 framebuffer_width;
u32 framebuffer_height;
u64 framebuffer_size;

struct display_info {
  u32 width;
  u32 height;
  u8 bpp;
};

struct display_info vbe_info;

#define place_pixel_pos(_p, _pos)                                              \
  { *(u32 *)((u32 *)framebuffer + _pos) = _p; }

bool display_driver_init(struct multiboot_tag_framebuffer_common *mbi) {
  framebuffer_width = mbi->framebuffer_width;
  framebuffer_height = mbi->framebuffer_height;

  u32 bits_pp = mbi->framebuffer_bpp;
  u32 bytes_pp = (bits_pp / 8) + (8 - (bits_pp % 8));

  framebuffer_size = bytes_pp * framebuffer_width * framebuffer_height;

  framebuffer_physical = mbi->framebuffer_addr;
  framebuffer = mmu_map_frames((void *)mbi->framebuffer_addr, framebuffer_size);
  if (!framebuffer) {
    return false;
  }

  vbe_info.width = framebuffer_width;
  vbe_info.height = framebuffer_height;
  vbe_info.bpp = mbi->framebuffer_bpp;

  memset(framebuffer, 0xFF, framebuffer_width * 20);
  return true;
}
