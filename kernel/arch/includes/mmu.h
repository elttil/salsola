#ifndef MMU_H
#define MMU_H
#include <error.h>
#include <stdbool.h>
#include <stddef.h>
#include <sv.h>
#include <typedefs.h>

#define MMU_FLAG_PRESENT (1 << 0)
#define MMU_FLAG_RW (1 << 1)
#define MMU_FLAG_USER (1 << 2)
#define MMU_FLAG_PCD (1 << 4)

#define align_up_int(address, alignment)                                           \
  ((0 == ((uintptr_t)address) % ((uintptr_t)alignment))                        \
       ? (address)                                                             \
       : (uintptr_t)((((uintptr_t)address) -                             \
                            ((uintptr_t)address % alignment)) +                \
                           ((uintptr_t)alignment)))

#define align_up_ptr(address, alignment)                                           \
  ((0 == ((uintptr_t)address) % ((uintptr_t)alignment))                        \
       ? (address)                                                             \
       : (void*)((((uintptr_t)address) -                             \
                            ((uintptr_t)address % alignment)) +                \
                           ((uintptr_t)alignment)))

#define PAGE_SIZE 0x1000

struct PML4T;
struct mmu_directory {
  struct PML4T *pml4t;
  void *physical;
};

void *ksbrk(size_t length);
void *ksbrk_physical(size_t length, void **physical);
int mmu_init(void *multiboot_header);
void *mmu_virtual_to_physical(void *address, bool *exists);
void *mmu_physical_to_virtual(void *address, bool *exists);
void *mmu_map_frames(void *src, size_t length);
void mmu_update_stack(void (*function)());
struct mmu_directory *mmu_clone_directory(struct mmu_directory *directory);
struct mmu_directory *mmu_get_active_directory(void);
void mmu_set_directory(struct mmu_directory *directory);
void mmu_unmap_frames(void *src, size_t length);
void mmu_remove_identity(void);
void mmu_init_for_new_core(void (*main)(void));
bool mmu_allocate_region(void *address, size_t length, int flags);
void mmu_lazy_set_directory(struct mmu_directory *directory);
err_t mmu_get_user_sv(char *string, size_t length, struct sv *s);
err_t mmu_assign_user_ptr(void *dst, const void *src, size_t size);
err_t mmu_verify_user_pointer(const void *ptr, u64 length);
err_t mmu_setup_random_region(void *address, size_t length, bool is_userspace,
                              bool allocate, int flags, void **out);
void *mmu_map_frames_to_region(void *src, size_t length, void *virtual,
                               int flags);
#endif // MMU_H
