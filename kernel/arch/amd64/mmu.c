#include <assert.h>
#include <kprintf.h>
#include <mmu.h>
#include <multiboot2.h>
#include <prng.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void flush_tlb(void);

#define PAGE_SIZE 0x1000

#define PAGE_FLAG_PRESENT (1 << 0)

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

bool check_virtual_region_is_free(void *address, void **physical, bool allocate,
                                  bool use_frame, void *frame);

// Depends upon a C version after C99 since it uses `typeof`
#define align_up(address, alignment)                                           \
  ((0 == ((uintptr_t)address) % ((uintptr_t)alignment))                        \
       ? (address)                                                             \
       : (typeof(address))((((uintptr_t)address) -                             \
                            ((uintptr_t)address % alignment)) +                \
                           ((uintptr_t)alignment)))

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

struct PML4T *active_directory;

#define NUM_OF_FRAMES 256

uint64_t frames[NUM_OF_FRAMES];
size_t num_pages = 0;

static inline bool set_frame(void *address, bool state) {
  uintptr_t a = (uintptr_t)address;
  a /= 0x1000;
  size_t index = a / 64;
  if (index >= NUM_OF_FRAMES) {
    return false;
  }
  size_t offset = a % 64;
  if (state) {
    frames[index] |= (1 << offset);
  } else {
    frames[index] &= ~(1 << offset);
  }
  return true;
}

void *get_frame(bool allocate, u64 count) {
  assert(0 != count);
  u64 left = count;
  void *rc = NULL;
  for (size_t i = 0; i < NUM_OF_FRAMES; i++) {
    if (~((uint64_t)0) == frames[i]) {
      left = count;
      continue;
    }

    for (size_t j = 0; j < 64; j++) {
      if (frames[i] & (1 << j)) {
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
          for (u64 i = 0; i < count; i++) {
            set_frame((void *)ptr, true);
            ptr += PAGE_SIZE;
          }
        }
        return (void *)rc;
      }
    }
  }
  assert(0);
  return NULL;
}

void allocate_next_pt(void *address);

void *heap_end;

void *mmu_find_free_virtual_region(size_t length) {
  for (size_t offset = 0;; offset += PAGE_SIZE) {
    bool is_free = true;
    for (size_t i = 0; i < length; i += PAGE_SIZE) {
      void *address = (void *)((uintptr_t)heap_end + offset + i);
      if (!check_virtual_region_is_free(address, NULL, false, false, NULL)) {
        is_free = false;
        break;
      }
    }
    if (is_free) {
      return (void *)((uintptr_t)heap_end + offset);
    }
  }
  assert(0);
  return NULL;
}

void *mmu_map_frames(void *src, size_t length) {
  void *virtual = mmu_find_free_virtual_region(length);

  uintptr_t p = (uintptr_t)src;
  for (size_t i = 0; i < length; i += PAGE_SIZE) {
    assert(check_virtual_region_is_free((void *)((uintptr_t) virtual + i), NULL,
                                        true, true, (void *)p));
    p += PAGE_SIZE;
  }

  return virtual;
}

// FIXME: WARNING: The allocation is not guaranteed to be linear in the
// physical memory mapping.
void *ksbrk_physical(size_t length, void **physical) {
  heap_end = mmu_find_free_virtual_region(length);
  void *rc = heap_end;

  allocate_next_pt(heap_end);
  if (0 == length) {
    return NULL;
  }
  heap_end = mmu_find_free_virtual_region(length);
  rc = heap_end;

  void *r = NULL;
  for (size_t i = 0; i < length; i += 0x1000) {
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
    bool was_free =
        check_virtual_region_is_free(heap_end, &physical, true, false, NULL);
    assert(was_free);

    if (!r) {
      r = physical;
    }
    heap_end += 0x1000;
  }
  if (physical) {
    *physical = r;
  }

  prng_get_pseudorandom(rc, align_up(length, PAGE_SIZE));
  return rc;
}

void *ksbrk(size_t length) {
  return ksbrk_physical(length, NULL);
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

  if (!(active_directory->physical[pml4t_index] & PAGE_FLAG_PRESENT)) {
    return NULL;
  }
  if (!(active_directory->pdpt[pml4t_index]->physical[pdpt_index] &
        PAGE_FLAG_PRESENT)) {
    return NULL;
  }
  if (!(active_directory->pdpt[pml4t_index]
            ->pdt[pdpt_index]
            ->physical[pdt_index] &
        PAGE_FLAG_PRESENT)) {
    return NULL;
  }
  if (!(active_directory->physical[pml4t_index] & PAGE_FLAG_PRESENT)) {
    return NULL;
  }

  uintptr_t p = active_directory->pdpt[pml4t_index]
                    ->pdt[pdpt_index]
                    ->pt[pdt_index]
                    ->page[pt_index];

  if (!(p & PAGE_FLAG_PRESENT)) {
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
  void *p = get_frame(true, (align_up(length, PAGE_SIZE)) / PAGE_SIZE);
  void *a = mmu_find_free_virtual_region(length);

  if (physical) {
    *physical = p;
  }

  uintptr_t ptr = (uintptr_t)a;
  uintptr_t phys_ptr = (uintptr_t)p;
  for (size_t i = 0; i < length / PAGE_SIZE; i++) {
    assert(check_virtual_region_is_free((void *)ptr, NULL, true, true,
                                        (void *)phys_ptr));
    phys_ptr += PAGE_SIZE;
    ptr += PAGE_SIZE;
  }
  memset(a, 0, align_up(length, PAGE_SIZE));
  return a;
}

bool allocate_pt(u64 pml4t_index, u64 pdpt_index, u64 pdt_index) {
  if (!(active_directory->physical[pml4t_index] & PAGE_FLAG_PRESENT)) {
    void *physical;
    struct PDPT *pdpt = safe_allocation(sizeof(struct PDPT), &physical);
    active_directory->physical[pml4t_index] = (uintptr_t)physical | 0x3;
    active_directory->pdpt[pml4t_index] = pdpt;
  }

  if (!(active_directory->pdpt[pml4t_index]->physical[pdpt_index] &
        PAGE_FLAG_PRESENT)) {
    void *physical;
    struct PDT *pdt = safe_allocation(sizeof(struct PDT), &physical);
    active_directory->pdpt[pml4t_index]->physical[pdpt_index] =
        (uintptr_t)physical | 0x3;
    active_directory->pdpt[pml4t_index]->pdt[pdpt_index] = pdt;
  }

  if ((active_directory->pdpt[pml4t_index]
           ->pdt[pdpt_index]
           ->physical[pdt_index] &
       PAGE_FLAG_PRESENT)) {
    return false;
  }

  void *physical;
  void *address = safe_allocation(sizeof(struct PT), &physical);

  active_directory->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] =
      (uintptr_t)physical | 0x3;
  active_directory->pdpt[pml4t_index]->pdt[pdpt_index]->pt[pdt_index] = address;

  return true;
}

void allocate_next_pt(void *address) {
  //  const int PT_SHIFT = 12;
  const int PDT_SHIFT = 12 + 9 * 1;
  const int PDPT_SHIFT = 12 + 9 * 2;
  const int PML4_SHIFT = 12 + 9 * 3;

  uint64_t pml4t_index = ((uintptr_t)address >> PML4_SHIFT) & 0x1FF;
  uint64_t pdpt_index = ((uintptr_t)address >> PDPT_SHIFT) & 0x1FF;
  uint64_t pdt_index = ((uintptr_t)address >> PDT_SHIFT) & 0x1FF;
  //  uint64_t pt_index = ((uintptr_t)address >> PT_SHIFT) & 0x1FF;

  allocate_pt(pml4t_index, pdpt_index, pdt_index);
  allocate_pt(pml4t_index, pdpt_index, pdt_index + 1);
}

// if allocate == false:
//   Returns true if the region does not exist.
//   Return false if the region does exist
// if allocate == true:
//   Returns false if region already exists
//   Returns false if region did not allocate
//   Return true if the region did not exist and was allocated
bool check_virtual_region_is_free(void *address, void **physical, bool allocate,
                                  bool use_frame, void *frame) {
  const int PT_SHIFT = 12;
  const int PDT_SHIFT = 12 + 9 * 1;
  const int PDPT_SHIFT = 12 + 9 * 2;
  const int PML4_SHIFT = 12 + 9 * 3;

  uint64_t pml4t_index = ((uintptr_t)address >> PML4_SHIFT) & 0x1FF;
  uint64_t pdpt_index = ((uintptr_t)address >> PDPT_SHIFT) & 0x1FF;
  uint64_t pdt_index = ((uintptr_t)address >> PDT_SHIFT) & 0x1FF;
  uint64_t pt_index = ((uintptr_t)address >> PT_SHIFT) & 0x1FF;

  bool region_exists = true;

  allocate_pt(pml4t_index, pdpt_index, pdt_index);
  /*
  if (!(active_directory->physical[pml4t_index] & 0x1)) {
    kprintf("ERROR 1\n");
    assert(!allocate);
    region_exists = false;
    goto check_return;
  }
  if (!(active_directory->pdpt[pml4t_index]->physical[pdpt_index] & 0x1)) {
    kprintf("ERROR 2\n");
    assert(!allocate);
    region_exists = false;
    goto check_return;
  }
  if (!(active_directory->pdpt[pml4t_index]
            ->pdt[pdpt_index]
            ->physical[pdt_index] &
        0x1)) {
    assert(!allocate);
    region_exists = false;
    goto check_return;
  }
  */

  void **p = (void **)&active_directory->pdpt[pml4t_index]
                 ->pdt[pdpt_index]
                 ->pt[pdt_index]
                 ->page[pt_index];

  if (!(((uintptr_t)*p) & PAGE_FLAG_PRESENT)) {
    // Region does not exist and we allocate it.
    if (allocate) {
      if (!use_frame) {
        frame = get_frame(true, 1);
      }
      *p = frame;
      *p = (void *)((uintptr_t)*p | 0x3);
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

// Moves the stack to its own PDPT.
// NOTE: Index starts counting from 0
// PDPT index 511 is reserved for the shared kernel address space.
// PDPT index 510 is exlusivley used for the stack which of course
// is not shared, but instead is copied.
void mmu_update_stack(void (*function)()) {
  void *new_stack = (void *)0xffffff8000000000 - 0x1000;

  size_t stack_size = 0x8000;

  for (size_t i = 0; i < stack_size; i += PAGE_SIZE) {
    assert(check_virtual_region_is_free((void *)((uintptr_t)new_stack - i),
                                        NULL, true, false, NULL));
  }

  goto_function_with_stack(function, new_stack);
}

int mmu_init(void *multiboot_header) {
  active_directory = (struct PML4T *)(((uintptr_t)&PML4T) + 0xffffff8000000000);

  heap_end = align_up(&_kernel_end, 0x1000);
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
      // FIXME: This is garbage, just memset
      for (uint32_t p = entry->addr; p < entry->addr + entry->len;
           p += 0x1000) {
        if (!set_frame((void *)p, false)) {
          break;
        }
      }
      assert(0 == entry->zero);
    }
  }

  for (size_t i = 0; i < 512; i++) {
    uintptr_t p = active_directory->physical[i] + 0xFFFFFF8000000000;
    if (!(p & PAGE_FLAG_PRESENT)) {
      continue;
    }

    struct PDPT *pdpt = (struct PDPT *)(p & ~(0xFFF));
    active_directory->pdpt[i] = pdpt;

    for (size_t j = 0; j < 512; j++) {
      uintptr_t physical = pdpt->physical[j] & ~(0xFFF);
      set_frame((void *)physical, true);

      uintptr_t p = pdpt->physical[j] + 0xFFFFFF8000000000;
      if (!(p & PAGE_FLAG_PRESENT)) {
        continue;
      }
      struct PDT *pdt = (struct PDT *)(p & ~(0xFFF));
      pdpt->pdt[j] = pdt;
      for (size_t c = 0; c < 512; c++) {
        uintptr_t physical = pdt->physical[c] & ~(0xFFF);
        set_frame((void *)physical, true);

        uintptr_t p = pdt->physical[c] + 0xFFFFFF8000000000;
        if (!(p & PAGE_FLAG_PRESENT)) {
          continue;
        }
        struct PT *pt = (struct PT *)(p & ~(0xFFF));
        pdt->pt[c] = pt;
        for (int k = 0; k < 512; k++) {
          uintptr_t physical = pt->page[k] & ~(0xFFF);
          set_frame((void *)physical, true);
        }
      }
    }
  }
  set_frame(&PML4T, true);

  active_directory->pdpt[0] = NULL;
  active_directory->physical[0] = (uintptr_t)NULL;

  ksbrk(0x0);

  flush_tlb();

  return 1;
}
