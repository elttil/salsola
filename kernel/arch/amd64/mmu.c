#include <assert.h>
#include <kprintf.h>
#include <mmu.h>
#include <multiboot2.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

bool check_virtual_region_is_free(void *address, void **physical,
                                  bool allocate);

// Depends upon a C version after C99 since it uses `typeof`
#define align_up(address, alignment)                                           \
  (0 == ((uintptr_t)address) % ((uintptr_t)alignment))                         \
      ? (address)                                                              \
      : (typeof(address))((((uintptr_t)address) -                              \
                           ((uintptr_t)address % alignment)) +                 \
                          ((uintptr_t)alignment));

/*
        align 4096
PML4T:
        resb 4096
PDPT:
        resb 4096
PDT:
        resb 4096
PT:
        resb 4096
  */

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

struct PML4T *kernel;

#define NUM_OF_FRAMES 256

uint64_t frames[NUM_OF_FRAMES];
size_t num_pages = 0;

static inline void set_frame(uintptr_t address, bool state) {
  address /= 0x1000;
  size_t index = address / 64;
  if (index >= NUM_OF_FRAMES) {
    return;
  }
  size_t offset = address % 64;
  if (state) {
    frames[index] |= (1 << offset);
  } else {
    frames[index] &= ~(1 << offset);
  }
}

void *get_frame(bool allocate) {
  for (size_t i = 0; i < NUM_OF_FRAMES; i++) {
    if (~((uint64_t)0) == frames[i]) {
      continue;
    }

    for (size_t j = 0; j < 64; j++) {
      if (frames[i] & (1 << j)) {
        continue;
      }
      uintptr_t rc = ((i * 64 + j) * 0x1000);
      if (allocate) {
        set_frame(rc, true);
      }
      return (void *)rc;
    }
  }
  assert(0);
  return NULL;
}

void allocate_next_pdt(void *address);

bool in_bootstrap_phase = true;

void *heap_end;

void *mmu_find_free_virtual_region(size_t length) {
  for (size_t offset = 0;; offset += PAGE_SIZE) {
    bool is_free = true;
    for (size_t i = 0; i < length; i += PAGE_SIZE) {
      void *address = (void *)((uintptr_t)heap_end + offset + i);
      if (!check_virtual_region_is_free(address, NULL, false)) {
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

// FIXME: WARNING: The allocation is not guaranteed to be linear in the
// physical memory mapping.
void *ksbrk_physical(size_t length, void **physical, bool a) {
  void *rc = heap_end;
  if (!in_bootstrap_phase) {
    heap_end = mmu_find_free_virtual_region(length);
    rc = heap_end;
  }

  if (!a) {
    allocate_next_pdt(heap_end);
    heap_end = mmu_find_free_virtual_region(length);
    rc = heap_end;
  }
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
    bool was_free = check_virtual_region_is_free(heap_end, &physical, true);
    if (!in_bootstrap_phase) {
      assert(was_free);
    }

    if (!r) {
      r = physical;
    }
    heap_end += 0x1000;
  }
  if (physical) {
    *physical = r;
  }
  in_bootstrap_phase = false;
  return rc;
}

void *ksbrk(size_t length) {
  return ksbrk_physical(length, NULL, false);
}

void *mmu_virtual_to_physical(void *address) {
  const int PT_SHIFT = 12;
  const int PDT_SHIFT = 12 + 9 * 1;
  const int PDPT_SHIFT = 12 + 9 * 2;
  const int PML4_SHIFT = 12 + 9 * 3;

  uint64_t pml4t_index = ((uintptr_t)address >> PML4_SHIFT) & 0x1FF;
  uint64_t pdpt_index = ((uintptr_t)address >> PDPT_SHIFT) & 0x1FF;
  uint64_t pdt_index = ((uintptr_t)address >> PDT_SHIFT) & 0x1FF;
  uint64_t pt_index = ((uintptr_t)address >> PT_SHIFT) & 0x1FF;

  if (!(kernel->physical[pml4t_index] & PAGE_FLAG_PRESENT)) {
    return NULL;
  }
  if (!(kernel->pdpt[pml4t_index]->physical[pdpt_index] & PAGE_FLAG_PRESENT)) {
    return NULL;
  }
  if (!(kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] &
        PAGE_FLAG_PRESENT)) {
    return NULL;
  }
  if (!(kernel->physical[pml4t_index] & PAGE_FLAG_PRESENT)) {
    return NULL;
  }

  uintptr_t p =
      kernel->pdpt[pml4t_index]->pdt[pdpt_index]->pt[pdt_index]->page[pt_index];

  if (!(p & PAGE_FLAG_PRESENT)) {
    return NULL;
  }

  p &= ~(0xFFF);
  p |= (uintptr_t)address & 0xFFF;
  return (void *)p;
}

void allocate_next_pdt(void *address) {
  //  const int PT_SHIFT = 12;
  const int PDT_SHIFT = 12 + 9 * 1;
  const int PDPT_SHIFT = 12 + 9 * 2;
  const int PML4_SHIFT = 12 + 9 * 3;

  uint64_t pml4t_index = ((uintptr_t)address >> PML4_SHIFT) & 0x1FF;
  uint64_t pdpt_index = ((uintptr_t)address >> PDPT_SHIFT) & 0x1FF;
  uint64_t pdt_index = ((uintptr_t)address >> PDT_SHIFT) & 0x1FF;
  //  uint64_t pt_index = ((uintptr_t)address >> PT_SHIFT) & 0x1FF;

  if (!(kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] &
        PAGE_FLAG_PRESENT)) {
    void *physical;
    void *address = ksbrk_physical(sizeof(struct PT), &physical, 1);
    memset(address, 0, sizeof(struct PT));

    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] =
        (uintptr_t)physical | 0x3;
    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->pt[pdt_index] = address;
  }
  pdt_index++;
  if (!(kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] &
        PAGE_FLAG_PRESENT)) {
    void *physical;
    void *address = ksbrk_physical(sizeof(struct PT), &physical, 1);
    memset(address, 0, sizeof(struct PT));

    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] =
        (uintptr_t)physical | 0x3;
    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->pt[pdt_index] = address;
  }
}

// if allocate == false:
//   Returns true if the region does not exist.
//   Return false if the region does exist
// if allocate == true:
//   Returns false if region already exists
//   Returns false if region did not allocate
//   Return true if the region did not exist and was allocated
bool check_virtual_region_is_free(void *address, void **physical,
                                  bool allocate) {
  const int PT_SHIFT = 12;
  const int PDT_SHIFT = 12 + 9 * 1;
  const int PDPT_SHIFT = 12 + 9 * 2;
  const int PML4_SHIFT = 12 + 9 * 3;

  uint64_t pml4t_index = ((uintptr_t)address >> PML4_SHIFT) & 0x1FF;
  uint64_t pdpt_index = ((uintptr_t)address >> PDPT_SHIFT) & 0x1FF;
  uint64_t pdt_index = ((uintptr_t)address >> PDT_SHIFT) & 0x1FF;
  uint64_t pt_index = ((uintptr_t)address >> PT_SHIFT) & 0x1FF;

  bool region_exists = true;

  if (!(kernel->physical[pml4t_index] & 0x1)) {
    kprintf("ERROR 1\n");
    assert(!allocate);
    region_exists = false;
    goto check_return;
  }
  if (!(kernel->pdpt[pml4t_index]->physical[pdpt_index] & 0x1)) {
    kprintf("ERROR 2\n");
    assert(!allocate);
    region_exists = false;
    goto check_return;
  }
  if (!(kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] &
        0x1)) {
    if (!allocate) {
      region_exists = false;
      goto check_return;
    }
    void *physical;
    void *address = ksbrk_physical(sizeof(struct PT), &physical, 0);
    memset(address, 0, sizeof(struct PT));

    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] =
        (uintptr_t)physical & 0x3;
    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->pt[pdt_index] = address;
  }

  void **p = (void **)&kernel->pdpt[pml4t_index]
                 ->pdt[pdpt_index]
                 ->pt[pdt_index]
                 ->page[pt_index];

  if (!(((uintptr_t)*p) & PAGE_FLAG_PRESENT)) {
    // Region does not exist and we allocate it.
    if (allocate) {
      *p = get_frame(true);
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
  if (!allocate) {
    return !region_exists;
  } else {
    //    if (region_exists) {
    //      return false;
    //    }
    return false;
  }
}

int mmu_init(void *multiboot_header) {
  kernel = (struct PML4T *)(((uintptr_t)&PML4T) + 0xffffff8000000000);

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
    for (uint32_t i = 0; i < m->size / m->entry_size; i++) {
      multiboot_memory_map_t *entry = &m->entries[i];
      if (MULTIBOOT_MEMORY_AVAILABLE != entry->type) {
        continue;
      }
      // FIXME: This is garbage, just memset
      for (uint32_t p = entry->addr; p < entry->addr + entry->len;
           p += 0x1000) {
        set_frame(p, false);
      }
      assert(0 == entry->zero);
    }
  }

  for (size_t i = 0; i < 512; i++) {
    uintptr_t p = kernel->physical[i] + 0xFFFFFF8000000000;
    if (!(p & PAGE_FLAG_PRESENT)) {
      continue;
    }

    struct PDPT *pdpt = (struct PDPT *)(p & ~(0xFFF));
    kernel->pdpt[i] = pdpt;

    for (size_t j = 0; j < 512; j++) {
      uintptr_t physical = pdpt->physical[j] & ~(0xFFF);
      set_frame(physical, true);

      uintptr_t p = pdpt->physical[j] + 0xFFFFFF8000000000;
      if (!(p & PAGE_FLAG_PRESENT)) {
        continue;
      }
      struct PDT *pdt = (struct PDT *)(p & ~(0xFFF));
      pdpt->pdt[j] = pdt;
      for (size_t c = 0; c < 512; c++) {
        uintptr_t physical = pdt->physical[c] & ~(0xFFF);
        set_frame(physical, true);

        uintptr_t p = pdt->physical[c] + 0xFFFFFF8000000000;
        if (!(p & PAGE_FLAG_PRESENT)) {
          continue;
        }
        struct PT *pt = (struct PT *)(p & ~(0xFFF));
        pdt->pt[c] = pt;
        for (int k = 0; k < 512; k++) {
          uintptr_t physical = pt->page[k] & ~(0xFFF);
          set_frame(physical, true);
        }
      }
    }
  }

  kernel->pdpt[0] = NULL;
  kernel->physical[0] = (uintptr_t)NULL;

  return 1;
}
