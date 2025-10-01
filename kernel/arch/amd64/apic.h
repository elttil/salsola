#include <stdbool.h>
#include <typedefs.h>

bool apic_enable(void);
bool apic_check(void);
void* apic_get_base(void);
void apic_set_base(void* apic);
