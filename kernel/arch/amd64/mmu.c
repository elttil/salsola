#include <arch/amd64/smp.h>
#include <assert.h>
#include <csprng.h>
#include <mmu.h>
#include <multiboot2.h>
#include <prng.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sv.h>
#include <sys/mman.h>

bool active_bootstrap = true;

void flush_tlb(void);

#define NULL_FRAME 0

struct PT {
  uintptr_t page[512];
};

struct PDT {
  uintptr_t physical[512];
  struct PT *pt[512];
};

struct PDPT {
  uintptr_t physical[512];
  struct PDT *pdt[512];
};

struct PML4T {
  uintptr_t physical[512];
  struct PDPT *pdpt[512];
};

#define MMU_PAGE_RANGE 0x1000
#define MMU_PT_RANGE ((u64)(MMU_PAGE_RANGE) * 512)
#define MMU_PDT_RANGE ((u64)(MMU_PT_RANGE) * 512)
#define MMU_PDPT_RANGE ((u64)(MMU_PDT_RANGE) * 512)

static bool check_virtual_region_is_free(void *address, void **physical,
                                         bool allocate, bool use_frame,
                                         void *frame, u32 flags);
static uintptr_t *get_page(void *src, void **next);
static bool get_page2(void *address, bool allocate_parents, int flags,
                      void ***page);

extern struct PML4T PML4T;

// extern void *Realm64;
// extern void *_kernel_start;
extern void *_kernel_end;

uint64_t pow(uint64_t a, uint64_t b) {
  uint64_t r = 1;
  for (; b > 0; b--) {
    r *= a;
  }
  return r;
}

err_t mmu_get_user_sv(const char *string, size_t length, struct sv *s) {
  // TODO: Validate the string
  PTR_ASSIGN(s, sv_init(string, length));
  return ERROR_SUCCESS;
}

err_t mmu_assign_user_ptr(void *dst, const void *src, size_t size) {
  // TODO: Validate user pointer
  memcpy(dst, src, size);
  return ERROR_SUCCESS;
}

err_t mmu_verify_user_pointer(const void *ptr, u64 length) {
  (void)ptr;
  (void)length;
  // TODO: Validate user pointer
  return ERROR_SUCCESS;
}

err_t mmu_verify_user_c_string(const char *ptr, size_t *size) {
  (void)ptr;
  (void)size;
  return ERROR_SUCCESS;
}

struct mmu_directory orig_active_directory;

#define NUM_OF_FRAMES (4096 * 4)

lock_t frame_lock;
uint64_t frames[NUM_OF_FRAMES];
size_t num_pages = 0;

static inline bool set_frame(void *address, bool state, bool getlock) {
  if (getlock) {
    lock_acquire(&frame_lock);
  }
  uintptr_t a = (uintptr_t)address;
  a /= 0x1000;
  size_t index = a / 64;
  if (index >= NUM_OF_FRAMES) {
    if (getlock) {
      lock_release(&frame_lock);
    }
    return false;
  }
  size_t offset = a % 64;
  if (state) {
    frames[index] |= ((u64)1 << offset);
  } else {
    frames[index] &= ~((u64)1 << offset);
  }
  if (getlock) {
    lock_release(&frame_lock);
  }
  return true;
}

u64 mmu_num_free_frames(void) {
  u64 count = 0;
  for (size_t i = 0; i < NUM_OF_FRAMES; i++) {
    if (~((uint64_t)0) == frames[i]) {
      continue;
    }

    for (size_t j = 0; j < 64; j++) {
      // TODO: Fix this, it should be able to handle frame zero.
      if (j == 0 && i == 0) {
        continue;
      }
      if (frames[i] & ((u64)1 << j)) {
        continue;
      }
      count++;
    }
  }
  return count;
}

void *get_frame(bool allocate, u64 count) {
  lock_acquire(&frame_lock);
  assert(0 != count);
  u64 left = count;
  void *rc = NULL;
  for (size_t i = 0; i < NUM_OF_FRAMES; i++) {
    if (~((uint64_t)0) == frames[i]) {
      left = count;
      continue;
    }

    for (size_t j = 0; j < 64; j++) {
      // TODO: Fix this, it should be able to handle frame zero.
      if (j == 0 && i == 0) {
        continue;
      }
      if (frames[i] & ((u64)1 << j)) {
        left = count;
        continue;
      }
      if (left == count) {
        rc = (void *)((i * 64 + j) * 0x1000);
      }
      left--;

      if (0 == left) {
        if (allocate) {
          uintptr_t ptr = (uintptr_t)rc;
          for (u64 z = 0; z < count; z++) {
            set_frame((void *)ptr, true, false);
            ptr += PAGE_SIZE;
          }
        }
        assert(rc != 0);
        lock_release(&frame_lock);
        return (void *)rc;
      }
    }
  }
  lock_release(&frame_lock);
  assert(0);
  return NULL;
}

void allocate_next_pt(void *address, u32 flags);

void *heap_end;

bool mmu_is_region_free(void *address, size_t length) {
  for (size_t i = 0; i < length; i += PAGE_SIZE) {
    void *ptr = (void *)((uintptr_t)address + i);
    if (!check_virtual_region_is_free(ptr, NULL, false, false, NULL, 0)) {
      return false;
    }
  }
  return true;
}

void *mmu_find_free_virtual_region(size_t length) {
  for (size_t offset = 0;; offset += PAGE_SIZE) {
    if (mmu_is_region_free((void *)((uintptr_t)heap_end + offset), length)) {
      void *r = (void *)((uintptr_t)heap_end + offset);
      if (PAGE_SIZE == length) {
        heap_end = (void *)((uintptr_t)heap_end + offset + length);
      }
      return r;
    }
  }
  assert(0);
  return NULL;
}

void *mmu_map_frames_to_region(void *src, size_t length, void *virtual,
                               int flags) {
  uintptr_t p = (uintptr_t)src;
  for (size_t i = 0; i < length; i += PAGE_SIZE) {
    assert(check_virtual_region_is_free((void *)((uintptr_t) virtual + i), NULL,
                                        true, true, (void *)p,
                                        flags | MMU_FLAG_PRESENT));
    p += PAGE_SIZE;
  }

  uintptr_t offset = (uintptr_t)src & 0xFFF;
  virtual = (void *)((uintptr_t) virtual + offset);
  return virtual;
}

void *mmu_map_frames(void *src, size_t length, u16 flags) {
  void *virtual = mmu_find_free_virtual_region(length);

  uintptr_t p = (uintptr_t)src;
  for (size_t i = 0; i < length; i += PAGE_SIZE) {
    assert(check_virtual_region_is_free((void *)((uintptr_t) virtual + i), NULL,
                                        true, true, (void *)p, flags));
    p += PAGE_SIZE;
  }

  uintptr_t offset = (uintptr_t)src & 0xFFF;
  virtual = (void *)((uintptr_t) virtual + offset);
  return virtual;
}

static uintptr_t *get_page(void *src, void **next) {
  uintptr_t address = (uintptr_t)src;
  const int PT_SHIFT = 12;
  const int PDT_SHIFT = 12 + 9 * 1;
  const int PDPT_SHIFT = 12 + 9 * 2;
  const int PML4_SHIFT = 12 + 9 * 3;

  uint64_t pml4t_index = ((uintptr_t)address >> PML4_SHIFT) & 0x1FF;
  uint64_t pdpt_index = ((uintptr_t)address >> PDPT_SHIFT) & 0x1FF;
  uint64_t pdt_index = ((uintptr_t)address >> PDT_SHIFT) & 0x1FF;
  uint64_t pt_index = ((uintptr_t)address >> PT_SHIFT) & 0x1FF;

  struct mmu_directory *directory = mmu_get_active_directory();

  if (!(directory->pml4t->physical[pml4t_index] & MMU_FLAG_PRESENT)) {
    ASSIGN_PTR(next, align_next_ptr(src, MMU_PDPT_RANGE));
    return NULL;
  }

  if (!(directory->pml4t->pdpt[pml4t_index]->physical[pdpt_index] &
        MMU_FLAG_PRESENT)) {
    ASSIGN_PTR(next, align_next_ptr(src, MMU_PDT_RANGE));
    return NULL;
  }

  if (!(directory->pml4t->pdpt[pml4t_index]
            ->pdt[pdpt_index]
            ->physical[pdt_index] &
        MMU_FLAG_PRESENT)) {
    ASSIGN_PTR(next, align_next_ptr(src, MMU_PT_RANGE));
    return NULL;
  }

  ASSIGN_PTR(next, align_next_ptr(src, MMU_PAGE_RANGE));
  return &directory->pml4t->pdpt[pml4t_index]
              ->pdt[pdpt_index]
              ->pt[pdt_index]
              ->page[pt_index];
}

void mmu_unmap_frames(void *src, u64 length, bool free_frames) {
  void *p = src;
  void *end = (void *)((uintptr_t)src + length);
  for (; p < end;) {
    uintptr_t *page = get_page(p, &p);
    if (!page) {
      continue;
    }
    if (0 == (*page & (~((uintptr_t)0xFFF)))) {
      continue;
    }
    if (free_frames) {
      set_frame((void *)*page, false, true);
    }
    *page = (uintptr_t)NULL;
  }
  // FIXME: Possibly expensive operation that may be best to avoid
  // if unmap_frames is called multiple times.
  flush_tlb();
}

void mmu_free_directory(struct mmu_directory *dir) {
  kfree(dir);
}

err_t mmu_setup_random_region(void *address, size_t length, bool is_userspace,
                              bool allocate, int flags, void **out) {
  // TODO: Use the address as a suggestion as to where the region should
  // be placed, for the NULL case the mapping will be truly random.
  // Maybe this should even be moved to a different function?
  (void)address;
  for (;;) {
    u64 address;
    if (is_userspace) {
      const u64 kernel_region_start = 0xF000000000;
      address = csprng_get_uniform(kernel_region_start);
      assert(!(address >= kernel_region_start));
    } else {
      // TODO:
      assert(0);
    }

    address &= ~(0xFFF);
    if (!mmu_is_region_free((void *)address, length)) {
      continue;
    }

    if (allocate) {
      mmu_allocate_region((void *)address, length, flags);
      flush_tlb();
    }

    ASSIGN_PTR(out, (void *)address);
    return ERROR_SUCCESS;
  }
}

// FIXME: WARNING: The allocation is not guaranteed to be linear in the
// physical memory mapping.
void *ksbrk_physical(size_t length, void **physical, bool fake) {
  heap_end = mmu_find_free_virtual_region(length);
  void *rc = heap_end;

  allocate_next_pt(heap_end, MMU_FLAG_PRESENT | MMU_FLAG_RW);
  if (0 == length) {
    return NULL;
  }
  heap_end = mmu_find_free_virtual_region(length);
  rc = heap_end;

  void *r = NULL;
  /*  for (size_t i = 0; i < length; i += 0x1000) {
      void *physical;

      // TODO: Maybe do something different? Current "problem" is that the
      // page table does get fully filled, but the regions are not
      // actually used since the kernel does not stretch that far, so we
      // can reuse them and save a lot of pain when it comes to
      // preallocating a table. Currently it is just using this
      // "bootstrapping" stage where it ignores if a region already is "in
      // use" such that it can allocate new "unused" tables for later
      // allocations. This does also mean some frames(and address space)
      // get lost forever, but it **should** not be that much. Maybe
      // allocate an extra table in boot.s to avoid this hack?
      bool was_free = check_virtual_region_is_free(
          heap_end, &physical, true, false, NULL,
          MMU_FLAG_RW | MMU_FLAG_PRESENT |
              ((fake) ? MMU_FLAG_FAKE_ALLOCATION : 0));
      assert(was_free);

      if (!r) {
        r = physical;
      }
      heap_end += 0x1000;
    }*/
  assert(mmu_allocate_region(heap_end, length,
                             MMU_FLAG_RW | MMU_FLAG_PRESENT |
                                 ((fake) ? MMU_FLAG_FAKE_ALLOCATION : 0)));

  r = mmu_virtual_to_physical(rc, NULL);

  if (physical) {
    *physical = r;
  }

  if (!fake) {
    memset(rc, 0, align_up_int(length, PAGE_SIZE));
  }
  return rc;
}

void *ksbrk(size_t length) {
  return ksbrk_physical(length, NULL, false);
}

void *mmu_virtual_to_physical(void *address, bool *exists) {
  if (exists) {
    *exists = false;
  }
  const int PT_SHIFT = 12;
  const int PDT_SHIFT = 12 + 9 * 1;
  const int PDPT_SHIFT = 12 + 9 * 2;
  const int PML4_SHIFT = 12 + 9 * 3;

  uint64_t pml4t_index = ((uintptr_t)address >> PML4_SHIFT) & 0x1FF;
  uint64_t pdpt_index = ((uintptr_t)address >> PDPT_SHIFT) & 0x1FF;
  uint64_t pdt_index = ((uintptr_t)address >> PDT_SHIFT) & 0x1FF;
  uint64_t pt_index = ((uintptr_t)address >> PT_SHIFT) & 0x1FF;

  struct mmu_directory *directory = mmu_get_active_directory();

  if (!(directory->pml4t->physical[pml4t_index] & MMU_FLAG_PRESENT)) {
    return NULL;
  }
  if (!(directory->pml4t->pdpt[pml4t_index]->physical[pdpt_index] &
        MMU_FLAG_PRESENT)) {
    return NULL;
  }
  if (!(directory->pml4t->pdpt[pml4t_index]
            ->pdt[pdpt_index]
            ->physical[pdt_index] &
        MMU_FLAG_PRESENT)) {
    return NULL;
  }

  uintptr_t p = directory->pml4t->pdpt[pml4t_index]
                    ->pdt[pdpt_index]
                    ->pt[pdt_index]
                    ->page[pt_index];

  if (!(p & MMU_FLAG_PRESENT)) {
    if (exists) {
      *exists = false;
    }
    return NULL;
  }
  if (exists) {
    *exists = true;
  }

  p &= ~(0xFFF);
  p |= (uintptr_t)address & 0xFFF;
  return (void *)p;
}

void *safe_allocation(size_t length, void **physical) {
  void *p = get_frame(true, (align_up_int(length, PAGE_SIZE)) / PAGE_SIZE);
  if (!p) {
    return NULL;
  }
  void *a = mmu_find_free_virtual_region(length);

  if (physical) {
    *physical = p;
  }

  uintptr_t ptr = (uintptr_t)a;
  uintptr_t phys_ptr = (uintptr_t)p;
  for (size_t i = 0; i < length / PAGE_SIZE; i++) {
    assert(check_virtual_region_is_free((void *)ptr, NULL, true, true,
                                        (void *)phys_ptr,
                                        MMU_FLAG_RW | MMU_FLAG_PRESENT));
    phys_ptr += PAGE_SIZE;
    ptr += PAGE_SIZE;
  }
  memset(a, 0, align_up_int(length, PAGE_SIZE));
  return a;
}

bool allocate_pt(u64 pml4t_index, u64 pdpt_index, u64 pdt_index, u32 flags) {
  flags |= MMU_FLAG_PRESENT;
  struct mmu_directory *directory = mmu_get_active_directory();

  if (!(directory->pml4t->physical[pml4t_index] & MMU_FLAG_PRESENT)) {
    void *physical;
    struct PDPT *pdpt = safe_allocation(sizeof(struct PDPT), &physical);
    if (!pdpt) {
      return false;
    }
    directory->pml4t->physical[pml4t_index] = (uintptr_t)physical | flags;
    directory->pml4t->pdpt[pml4t_index] = pdpt;
  }

  if (!(directory->pml4t->pdpt[pml4t_index]->physical[pdpt_index] &
        MMU_FLAG_PRESENT)) {
    void *physical;
    struct PDT *pdt = safe_allocation(sizeof(struct PDT), &physical);
    if (!pdt) {
      return false;
    }
    directory->pml4t->pdpt[pml4t_index]->physical[pdpt_index] =
        (uintptr_t)physical | flags;
    directory->pml4t->pdpt[pml4t_index]->pdt[pdpt_index] = pdt;
  }

  if ((directory->pml4t->pdpt[pml4t_index]
           ->pdt[pdpt_index]
           ->physical[pdt_index] &
       MMU_FLAG_PRESENT)) {
    return false;
  }

  void *physical;
  void *address = safe_allocation(sizeof(struct PT), &physical);
  if (!address) {
    return false;
  }

  directory->pml4t->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] =
      (uintptr_t)physical | flags;
  directory->pml4t->pdpt[pml4t_index]->pdt[pdpt_index]->pt[pdt_index] = address;

  return true;
}

void allocate_next_pt(void *address, u32 flags) {
  //  const int PT_SHIFT = 12;
  const int PDT_SHIFT = 12 + 9 * 1;
  const int PDPT_SHIFT = 12 + 9 * 2;
  const int PML4_SHIFT = 12 + 9 * 3;

  uint64_t pml4t_index = ((uintptr_t)address >> PML4_SHIFT) & 0x1FF;
  uint64_t pdpt_index = ((uintptr_t)address >> PDPT_SHIFT) & 0x1FF;
  uint64_t pdt_index = ((uintptr_t)address >> PDT_SHIFT) & 0x1FF;
  //  uint64_t pt_index = ((uintptr_t)address >> PT_SHIFT) & 0x1FF;

  allocate_pt(pml4t_index, pdpt_index, pdt_index, flags);
  if (pdt_index + 1 > 0x1FF) {
    return;
  }
  allocate_pt(pml4t_index, pdpt_index, pdt_index + 1, flags);
  allocate_pt(pml4t_index, pdpt_index + 1, 0, flags);
}

static bool get_page2(void *address, bool allocate_parents, int flags,
                      void ***page) {
  const int PT_SHIFT = 12;
  const int PDT_SHIFT = 12 + 9 * 1;
  const int PDPT_SHIFT = 12 + 9 * 2;
  const int PML4_SHIFT = 12 + 9 * 3;

  uint64_t pml4t_index = ((uintptr_t)address >> PML4_SHIFT) & 0x1FF;
  uint64_t pdpt_index = ((uintptr_t)address >> PDPT_SHIFT) & 0x1FF;
  uint64_t pdt_index = ((uintptr_t)address >> PDT_SHIFT) & 0x1FF;
  uint64_t pt_index = ((uintptr_t)address >> PT_SHIFT) & 0x1FF;

  struct mmu_directory *directory = mmu_get_active_directory();
  if (allocate_parents) {
    allocate_pt(pml4t_index, pdpt_index, pdt_index, flags);
  } else {
    if (!(directory->pml4t->physical[pml4t_index] & MMU_FLAG_PRESENT)) {
      return false;
    }
    if (!(directory->pml4t->pdpt[pml4t_index]->physical[pdpt_index] &
          MMU_FLAG_PRESENT)) {
      return false;
    }
    if (!(directory->pml4t->pdpt[pml4t_index]
              ->pdt[pdpt_index]
              ->physical[pdt_index] &
          MMU_FLAG_PRESENT)) {
      return false;
    }
  }
  ASSIGN_PTR(page, (void **)&directory->pml4t->pdpt[pml4t_index]
                       ->pdt[pdpt_index]
                       ->pt[pdt_index]
                       ->page[pt_index]);
  return true;
}

// if allocate == false:
//   Returns true if the region does not exist.
//   Return false if the region does exist
// if allocate == true:
//   Returns false if region already exists
//   Returns false if region did not allocate
//   Return true if the region did not exist and was allocated
static bool check_virtual_region_is_free(void *address, void **physical,
                                         bool allocate, bool use_frame,
                                         void *frame, u32 flags) {
  const int PT_SHIFT = 12;
  const int PDT_SHIFT = 12 + 9 * 1;
  const int PDPT_SHIFT = 12 + 9 * 2;
  const int PML4_SHIFT = 12 + 9 * 3;

  uint64_t pml4t_index = ((uintptr_t)address >> PML4_SHIFT) & 0x1FF;
  uint64_t pdpt_index = ((uintptr_t)address >> PDPT_SHIFT) & 0x1FF;
  uint64_t pdt_index = ((uintptr_t)address >> PDT_SHIFT) & 0x1FF;
  uint64_t pt_index = ((uintptr_t)address >> PT_SHIFT) & 0x1FF;

  bool region_exists = true;

  struct mmu_directory *directory = mmu_get_active_directory();
  if (allocate) {
    allocate_pt(pml4t_index, pdpt_index, pdt_index, flags);
  } else {
    if (!(directory->pml4t->physical[pml4t_index] & MMU_FLAG_PRESENT)) {
      return true;
    }
    if (!(directory->pml4t->pdpt[pml4t_index]->physical[pdpt_index] &
          MMU_FLAG_PRESENT)) {
      return true;
    }
    if (!(directory->pml4t->pdpt[pml4t_index]
              ->pdt[pdpt_index]
              ->physical[pdt_index] &
          MMU_FLAG_PRESENT)) {
      return true;
    }
  }

  void **p = (void **)&directory->pml4t->pdpt[pml4t_index]
                 ->pdt[pdpt_index]
                 ->pt[pdt_index]
                 ->page[pt_index];

  if (!(((uintptr_t)*p) & MMU_FLAG_PRESENT)) {
    // Region does not exist and we allocate it.
    if (allocate) {
      if (!use_frame && !(flags & MMU_FLAG_FAKE_ALLOCATION)) {
        frame = get_frame(true, 1);
        if (!frame) {
          return false;
        }
      }
      if (flags & MMU_FLAG_FAKE_ALLOCATION) {
        assert(!use_frame);
        frame = NULL_FRAME;
        flags &= 0xF;
        flags &= ~(MMU_FLAG_RW);
      }
      *p = frame;
      *p = (void *)((uintptr_t)*p | flags | MMU_FLAG_PRESENT);
      if (physical) {
        *physical = (void *)((uintptr_t)(*p) & ~(0xFFF));
      }

      return true;
    }
    region_exists = false;
    goto check_return;
  }
  if (physical) {
    *physical = (void *)((uintptr_t)(*p) & ~(0xFFF));
  }

check_return:
  // if allocate == false:
  //   Returns true if the region does not exist.
  //   Return false if the region does exist
  // if allocate == true:
  //   Returns false if region already exists
  //   Returns false if region did not allocate
  //   Return true if the region did not exist and was allocated
  if (allocate) {
    return false;
  } else {
    //    if (region_exists) {
    //      return false;
    //    }
    return !region_exists;
  }
}

void *get_current_sp(void);
void *get_current_sbp(void);

void set_sp(void *);
void set_sbp(void *);

void goto_function_with_stack(void *, void *);

size_t total_region_allocation = 0;

bool mmu_check_fake_allocation(void *address, bool allocate) {
  volatile void **ptr;
  if (!get_page2(address, false, 0, (void ***)&ptr)) {
    return false;
  }
  uintptr_t a = (uintptr_t)*ptr;
  uintptr_t frame = a & ~((uintptr_t)0xFFF);

  u8 flags = a & 0xFFF;
  if (!(flags & MMU_FLAG_PRESENT)) {
    return false;
  }

  if (frame != NULL_FRAME) {
    return false;
  }

  if (!allocate) {
    return true;
  }

  uintptr_t mod = (uintptr_t)*ptr;
  mod &= 0xFFF;
  mod |= (uintptr_t)get_frame(true, 1) | MMU_FLAG_RW;
  *ptr = (void *)mod;
  flush_tlb();
  void *va = (void *)(((uintptr_t)address) & ~(0xFFF));
  memset(va, 0, PAGE_SIZE);
  return true;
}

bool mmu_allocate_region(void *address, size_t length, int flags) {
  // NOTE: This is a simple fix for ELF files having strange alignment.
  // This function is not intended to have it supplied non aligned
  // addresses.
  uintptr_t a = (uintptr_t)address;
  if (0 != a % PAGE_SIZE) {
    a &= ~0xFFF;
    length += PAGE_SIZE;
    address = (void *)a;
  }

  if (flags & MMU_FLAG_FAKE_ALLOCATION) {
    assert(flags & MMU_FLAG_RW);
    for (size_t i = 0x0; i < length; i += PAGE_SIZE) {
      assert(check_virtual_region_is_free((void *)((uintptr_t)address + i),
                                          NULL, true, true, NULL_FRAME,
                                          (flags & 0xF) | MMU_FLAG_PRESENT));
    }
    flush_tlb();
    assert(mmu_virtual_to_physical(address, NULL) == NULL_FRAME);
    memset(address, 0, PAGE_SIZE);
    for (size_t i = 0x0; i < align_up_int(length, PAGE_SIZE); i += PAGE_SIZE) {
      volatile void **ptr;
      assert(get_page2(address + i, false, 0, (void ***)&ptr));
      uintptr_t a = (uintptr_t)*ptr;
      *ptr = (void *)(a & ~(MMU_FLAG_RW));
    }
    flush_tlb();
    return true;
  }

  total_region_allocation += length;
  size_t num_frames = length / PAGE_SIZE;
  if (0 == num_frames) {
    num_frames = 1;
  }
  void *frames = get_frame(true, num_frames);
  for (size_t i = 0x0; i < length; i += PAGE_SIZE) {
    //    assert(check_virtual_region_is_free((void *)((uintptr_t)address + i),
    //    NULL,
    //                                        true, false, NULL,
    //                                        flags | MMU_FLAG_PRESENT));
    assert(check_virtual_region_is_free(
        (void *)((uintptr_t)address + i), NULL, true, true,
        (void *)((uintptr_t)frames + i), flags | MMU_FLAG_PRESENT));
  }
  // TODO: Error check
  return true;
}

// Moves the stack to its own PDPT.
// NOTE: Index starts counting from 0
// PDPT index 511 is reserved for the shared kernel address space.
// PDPT index 510 is exlusivley used for the stack which of course
// is not shared, but instead is copied.
void mmu_update_stack(void (*function)()) {
  void *new_stack = (void *)0xffffff8000000000 - 0x1000 /*Guard page*/;

  size_t stack_size = 0xA000 + 0x4000;

  mmu_allocate_region(new_stack - stack_size, stack_size,
                      MMU_FLAG_RW | MMU_FLAG_PRESENT);

  goto_function_with_stack(function, new_stack);
}

void copy_frame(void *physical_dst, void *physical_src) {
  void *e = heap_end;
  void *dst = mmu_map_frames((void *)((uintptr_t)physical_dst & (~0xFFF)),
                             PAGE_SIZE, MMU_FLAG_RW | MMU_FLAG_PRESENT);
  void *src = mmu_map_frames((void *)((uintptr_t)physical_src & (~0xFFF)),
                             PAGE_SIZE, MMU_FLAG_RW | MMU_FLAG_PRESENT);

  assert(((uintptr_t)physical_dst & (~0xFFF)) ==
         ((uintptr_t)mmu_virtual_to_physical(dst, NULL)));
  assert(((uintptr_t)physical_src & (~0xFFF)) ==
         ((uintptr_t)mmu_virtual_to_physical(src, NULL)));

  memcpy(dst, src, PAGE_SIZE);

  mmu_unmap_frames(dst, PAGE_SIZE, false);
  mmu_unmap_frames(src, PAGE_SIZE, false);
  heap_end = e;
}

bool clone_pt(struct PT *orig_pt, struct PT **new_pt, void *virtual_address,
              void **physical) {
  *new_pt = safe_allocation(sizeof(struct PT), physical);
  if (!*new_pt) {
    return false;
  }

  for (int i = 0; i < 512; i++, virtual_address += MMU_PAGE_RANGE) {
    int flags = orig_pt->page[i] & 0xFFF;
    if (!(flags & MMU_FLAG_PRESENT)) {
      continue;
    }

    if (flags & MMU_FLAG_SHARED) {
      (*new_pt)->page[i] = orig_pt->page[i];
      continue;
    }

    (*new_pt)->page[i] = (uintptr_t)get_frame(true, 1) | flags;
    assert((*new_pt)->page[i]); // TODO:
    copy_frame((void *)((*new_pt)->page[i] & ~0xFFF), (void *)orig_pt->page[i]);
  }

  return true;
}

bool clone_pdt(struct PDT *orig_pdt, struct PDT **new_pdt,
               void *virtual_address, void **physical) {
  *new_pdt = safe_allocation(sizeof(struct PDT), physical);
  if (!*new_pdt) {
    return false;
  }

  for (int i = 0; i < 512; i++, virtual_address += MMU_PT_RANGE) {
    int flags = orig_pdt->physical[i] & 0xFFF;
    if (!(flags & MMU_FLAG_PRESENT)) {
      continue;
    }
    assert(clone_pt(orig_pdt->pt[i], &((*new_pdt)->pt[i]), virtual_address,
                    (void **)&((*new_pdt)->physical[i])));
    (*new_pdt)->physical[i] |= flags;
  }

  return true;
}

bool clone_pdpt(struct PDPT *orig_pdpt, struct PDPT **new_pdpt,
                void *virtual_address, void **physical) {
  *new_pdpt = safe_allocation(sizeof(struct PDPT), physical);
  if (!*new_pdpt) {
    return false;
  }

  for (int i = 0; i < 512; i++, virtual_address += MMU_PDT_RANGE) {
    int flags = orig_pdpt->physical[i] & 0xFFF;
    if (!(flags & MMU_FLAG_PRESENT)) {
      continue;
    }
    assert(clone_pdt(orig_pdpt->pdt[i], &((*new_pdpt)->pdt[i]), virtual_address,
                     (void **)&((*new_pdpt)->physical[i])));
    (*new_pdpt)->physical[i] |= flags;
  }

  return true;
}

struct mmu_directory *mmu_clone_directory(struct mmu_directory *directory) {
  //  struct mmu_directory *new_mmu_directory = ksbrk(sizeof(struct
  //  mmu_directory));
  struct mmu_directory *new_mmu_directory =
      kmalloc(sizeof(struct mmu_directory));

  void *physical;
  struct PML4T *pml4t = safe_allocation(sizeof(struct PML4T), &physical);
  if (!pml4t) {
    return NULL;
  }
  new_mmu_directory->pml4t = pml4t;
  new_mmu_directory->physical = physical;

  void *virtual_address = 0;
  for (int i = 0; i < 511; i++, virtual_address += MMU_PDPT_RANGE) {
    if (active_bootstrap && 0 == i) {
      continue;
    }
    int flags = directory->pml4t->physical[i] & 0xFFF;
    if (!(flags & MMU_FLAG_PRESENT)) {
      continue;
    }
    assert(clone_pdpt(directory->pml4t->pdpt[i], &pml4t->pdpt[i],
                      virtual_address, (void **)&pml4t->physical[i]));
    pml4t->physical[i] |= flags;
  }

  new_mmu_directory->pml4t->pdpt[511] = directory->pml4t->pdpt[511];
  new_mmu_directory->pml4t->physical[511] = directory->pml4t->physical[511];

  return new_mmu_directory;
}

// TODO: Put this in a header
void set_cr3(void *cr3);

void mmu_lazy_set_directory(struct mmu_directory *directory) {
  kernel_threads[core_id_get()].active_directory = directory;
}

void mmu_set_directory(struct mmu_directory *directory) {
  kernel_threads[core_id_get()].active_directory = directory;
  set_cr3(directory->physical);
}

void mmu_remove_identity(void) {
  struct mmu_directory *directory = mmu_get_active_directory();
  directory->pml4t->pdpt[0] = NULL;
  directory->pml4t->physical[0] = (uintptr_t)NULL;
  active_bootstrap = false;
}

struct mmu_directory *mmu_get_active_directory(void) {
  return kernel_threads[core_id_get()].active_directory;
}

void set_stack_and_jump(void *, void *);
void mmu_enable_write_protection(void);
void mmu_init_for_new_core(void (*main)(void)) {
  // Since we currently have no idea what the bootstrapping core is,
  // search through the kernel_threads, find a valid directory and use
  // that as the base.
  struct mmu_directory *base_directory = NULL;
  for (int i = 0; i < MAX_CORES; i++) {
    base_directory = kernel_threads[i].active_directory;
    if (base_directory) {
      break;
    }
  }
  assert(base_directory);

  // Set the directory now so we can do allocations
  mmu_set_directory(base_directory);
  mmu_enable_write_protection();

  struct mmu_directory *new_directory = mmu_clone_directory(base_directory);
  if (!new_directory) {
    assert(0);
    return;
  }

  new_directory->pml4t->pdpt[0] = base_directory->pml4t->pdpt[0];
  new_directory->pml4t->physical[0] = base_directory->pml4t->physical[0];

  mmu_set_directory(new_directory);

  void *new_stack = (void *)0xffffff8000000000 - 0x1000 /*GUARD PAGE*/;
  set_stack_and_jump(new_stack, main);
}

void set_frame_region(void *start, void *end, bool value) {
  for (uint32_t p = (uintptr_t)start; p < (uintptr_t)end; p += 0x1000) {
    if (!set_frame((void *)p, value, true)) {
      break;
    }
  }
}

int mmu_init(void *multiboot_header) {
  struct mmu_directory *active_directory = &orig_active_directory;
  kernel_threads[core_id_get()].active_directory = active_directory;

  active_directory->pml4t =
      (struct PML4T *)(((uintptr_t)&PML4T) + 0xffffff8000000000);
  active_directory->physical = &PML4T;

  heap_end = align_up_ptr(&_kernel_end, 0x1000);
  heap_end = (void *)((uintptr_t)heap_end + 0x1000);

  memset(frames, 0xFF, sizeof(frames));

  uintptr_t addr = (uintptr_t)multiboot_header + 0xFFFFFF8000000000;

  for (struct multiboot_tag *tag = (struct multiboot_tag *)(addr + 8);
       tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = (struct multiboot_tag *)((multiboot_uint8_t *)tag +
                                      ((tag->size + 7) & ~7))) {
    if (tag->type != MULTIBOOT_TAG_TYPE_MMAP) {
      continue;
    }

    struct multiboot_tag_mmap *m = (struct multiboot_tag_mmap *)tag;

    // FIXME: WARNING: Check if it actually should be m->size/m->entry_size
    // It could cause a lot of bugs if this is incorrect.
    unsigned int entries_count =
        (m->size - sizeof(struct multiboot_tag_mmap)) / m->entry_size;
    for (uint32_t i = 0; i < entries_count; i++) {
      multiboot_memory_map_t *entry = &m->entries[i];
      if (MULTIBOOT_MEMORY_AVAILABLE != entry->type) {
        continue;
      }
      set_frame_region((void *)entry->addr, (void *)(entry->addr + entry->len),
                       false);
      assert(0 == entry->zero);
    }
  }
  for (struct multiboot_tag *tag = (struct multiboot_tag *)(addr + 8);
       tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = (struct multiboot_tag *)((multiboot_uint8_t *)tag +
                                      ((tag->size + 7) & ~7))) {
    if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
      struct multiboot_tag_module *module = (struct multiboot_tag_module *)tag;

      set_frame_region((void *)module->mod_start, (void *)module->mod_end,
                       true);
    }
  }
  set_frame(NULL_FRAME, true, true);

  for (size_t i = 0; i < 512; i++) {
    uintptr_t p = active_directory->pml4t->physical[i] + 0xFFFFFF8000000000;
    if (!(p & MMU_FLAG_PRESENT)) {
      continue;
    }

    struct PDPT *pdpt = (struct PDPT *)(p & ~(0xFFF));
    active_directory->pml4t->pdpt[i] = pdpt;

    for (size_t j = 0; j < 512; j++) {
      uintptr_t physical = pdpt->physical[j] & ~(0xFFF);
      set_frame((void *)physical, true, true);

      uintptr_t p = pdpt->physical[j] + 0xFFFFFF8000000000;
      if (!(p & MMU_FLAG_PRESENT)) {
        continue;
      }
      struct PDT *pdt = (struct PDT *)(p & ~(0xFFF));
      pdpt->pdt[j] = pdt;
      for (size_t c = 0; c < 512; c++) {
        uintptr_t physical = pdt->physical[c] & ~(0xFFF);
        set_frame((void *)physical, true, true);

        uintptr_t p = pdt->physical[c] + 0xFFFFFF8000000000;
        if (!(p & MMU_FLAG_PRESENT)) {
          continue;
        }
        struct PT *pt = (struct PT *)(p & ~(0xFFF));
        pdt->pt[c] = pt;
        for (int k = 0; k < 512; k++) {
          if (i == 511) {
            break;
          }
          uintptr_t physical = pt->page[k] & ~(0xFFF);
          set_frame((void *)physical, true, true);
        }
      }
    }
  }
  set_frame(&PML4T, true, true);

  // FIXME: Shitty hack
  for (size_t i = 0; i < 20; i++) {
    allocate_next_pt(heap_end + 0x1000 * 1024 * i,
                     MMU_FLAG_PRESENT | MMU_FLAG_RW);
  }

  mmu_enable_write_protection();

  flush_tlb();
  return 1;
}
