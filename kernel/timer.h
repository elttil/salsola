#include <multiboot2.h>
#include <sv.h>

void timer_init(struct multiboot_tag *tags);
bool timer_add_device(struct sv filename);
u64 timer_get_ms(void);
u64 timer_get_us(void);
