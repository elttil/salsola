#include <stdbool.h>
#include <typedefs.h>

bool apic_enable(void);
bool apic_check(void);
volatile void *apic_get_base(void);
void apic_set_base(volatile void *apic);
void apic_timer_install(void);
