#include <stddef.h>

void *ksbrk(size_t length);
int mmu_init(void *multiboot_header);
void *mmu_virtual_to_physical(void *address);
