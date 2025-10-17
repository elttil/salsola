#include <multiboot2.h>
#include <stdbool.h>
#include <sv.h>

bool display_driver_init(struct multiboot_tag_framebuffer_common *mbi);
bool framebuffer_add_device(struct sv filename);
