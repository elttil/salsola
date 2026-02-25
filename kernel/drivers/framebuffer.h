#include <multiboot2.h>
#include <stdbool.h>
#include <typedefs.h>
#include <sv.h>

bool display_driver_init(struct multiboot_tag_framebuffer_common *mbi);
bool framebuffer_add_device(struct sv filename);
void framebuffer_clear_screen(u32 color);
void framebuffer_drawfont(u32 px, u32 py, const u8 c);
