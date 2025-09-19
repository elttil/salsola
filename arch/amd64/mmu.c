#include <assert.h>
#include <kprintf.h>
#include <mmu.h>
#include <multiboot2.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

uint64_t frames[256];
size_t num_pages = 0;

static inline void set_frame(uintptr_t address, bool state) {
  address /= 0x1000;
  size_t index = address / 64;
  if (index >= 256) {
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
  for (size_t i = 0; i < 256; i++) {
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

void *allocate_address(void *address);
void allocate_next_pdt(void *address);

void *heap_end;

void *ksbrk_physical(size_t length, void **physical, int a) {
  void *rc = heap_end;
  if (!a) {
    allocate_next_pdt(heap_end);
    rc = heap_end;
  }
  void *r = NULL;
  for (size_t i = 0; i < length; i += 0x1000) {
    void *l = allocate_address(heap_end);
    if (!r) {
      r = l;
    }
    heap_end += 0x1000;
  }
  if (physical) {
    *physical = r;
  }
  return rc;
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
    kprintf("ALLOCATE PDT FIRST %d\n", pdt_index);
    void *physical;
    void *address = ksbrk_physical(sizeof(struct PT), &physical, 1);

    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] =
        (uintptr_t)physical | 0x3;
    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->pt[pdt_index] = address;
  }
  pdt_index++;
  if (!(kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] &
        PAGE_FLAG_PRESENT)) {
    kprintf("ALLOCATE PDT SECOND %d\n", pdt_index);
    void *physical;
    void *address = ksbrk_physical(sizeof(struct PT), &physical, 1);

    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] =
        (uintptr_t)physical | 0x3;
    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->pt[pdt_index] = address;
  }
}

void *allocate_address(void *address) {
  const int PT_SHIFT = 12;
  const int PDT_SHIFT = 12 + 9 * 1;
  const int PDPT_SHIFT = 12 + 9 * 2;
  const int PML4_SHIFT = 12 + 9 * 3;

  uint64_t pml4t_index = ((uintptr_t)address >> PML4_SHIFT) & 0x1FF;
  uint64_t pdpt_index = ((uintptr_t)address >> PDPT_SHIFT) & 0x1FF;
  uint64_t pdt_index = ((uintptr_t)address >> PDT_SHIFT) & 0x1FF;
  uint64_t pt_index = ((uintptr_t)address >> PT_SHIFT) & 0x1FF;

  if (!(kernel->physical[pml4t_index] & 0x1)) {
    kprintf("ERROR 1\n");
    return NULL;
  }
  if (!(kernel->pdpt[pml4t_index]->physical[pdpt_index] & 0x1)) {
    kprintf("ERROR 2\n");
    return NULL;
  }
  if (!(kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] &
        0x1)) {
    kprintf("ALLOCATE PDT\n");
    void *physical;
    void *address = ksbrk_physical(sizeof(struct PT), &physical, 0);

    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] =
        (uintptr_t)physical & 0x3;
    kernel->pdpt[pml4t_index]->pdt[pdpt_index]->pt[pdt_index] = address;
  }

  uintptr_t *physical = &kernel->pdpt[pml4t_index]
                             ->pdt[pdpt_index]
                             ->pt[pdt_index]
                             ->page[pt_index];

  if (!(*physical & 0x1)) {
    *physical = (uintptr_t)get_frame(true);
    *physical |= 0x3;
  }
  return (void *)((*physical) & ~(0xFFF));
}

int mmu_init(void *multiboot_header) {
  // 0xffffff8000000000

  kernel = (struct PML4T *)(((uintptr_t)&PML4T) + 0xffffff8000000000);

  heap_end = align_up(&_kernel_end, 0x1000);
  heap_end = (void *)((uintptr_t)heap_end + 0x1000);

  for (size_t i = 0; i < 512; i++) {
    uintptr_t p = kernel->physical[i] + 0xFFFFFF8000000000;
    if (!(p & PAGE_FLAG_PRESENT)) {
      // TODO: Do we need to NULL them?
      //      second->pdpt[i] = NULL;
      continue;
    }

    struct PDPT *pdpt = (struct PDPT *)(p & ~(0xFFF));
    kernel->pdpt[i] = pdpt;

    for (size_t j = 0; j < 512; j++) {
      //      uintptr_t physical = pdpt->physical[j] & ~(0xFFF);

      uintptr_t p = pdpt->physical[j] + 0xFFFFFF8000000000;
      if (!(p & PAGE_FLAG_PRESENT)) {
        continue;
      }
      struct PDT *pdt = (struct PDT *)(p & ~(0xFFF));
      pdpt->pdt[j] = pdt;
      for (size_t c = 0; c < 512; c++) {
        //        uintptr_t physical = pdt->physical[c] & ~(0xFFF);

        uintptr_t p = pdt->physical[c] + 0xFFFFFF8000000000;
        if (!(p & PAGE_FLAG_PRESENT)) {
          continue;
        }
        struct PT *pt = (struct PT *)(p & ~(0xFFF));
        pdt->pt[c] = pt;
        for (int k = 0; k < 512; k++) {
          //          uintptr_t physical = pt->page[k] & ~(0xFFF);
        }
      }
    }
  }

  kernel->pdpt[0] = NULL;
  kernel->physical[0] = (uintptr_t)NULL;

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
    kprintf("m->entry_size: %u\n", m->entry_size);
    kprintf("m->size: %u\n", m->size);

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

  void *l = ksbrk_physical(0x10000, NULL, 0);
  kprintf("l: %x\n", l);
  memset(l, 0x41, 0x1000);
  l = ksbrk_physical(0x200000, NULL, 0);
  kprintf("l: %x\n", l);
  memset(l, 0x41, 0x200000);

  return 1;
}
