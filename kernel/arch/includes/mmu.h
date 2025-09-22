#include <stddef.h>

void *ksbrk(size_t length);
void *ksbrk_physical(size_t length, void **physical);
int mmu_init(void *multiboot_header);
void *mmu_virtual_to_physical(void *address, bool *exists);
void *mmu_physical_to_virtual(void *address, bool *exists);
void *mmu_map_frames(void *src, size_t length);
void mmu_update_stack(void (*function)());
