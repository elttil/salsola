#include <arch/amd64/smp.h>
#include <assert.h>
#include <kprintf.h>
#include <mmu.h>
#include <multiboot2.h>
#include <prng.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

bool active_bootstrap = true;

struct kernel_thread {
  struct mmu_directory *active_directory;
};

#define MAX_CORES 64
// FIXME: Limited to 64 cores
struct kernel_thread kernel_threads[MAX_CORES];

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

struct mmu_directory orig_active_directory;

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

  uintptr_t offset = (uintptr_t)src & 0xFFF;
  virtual = (void *)((uintptr_t) virtual + offset);
  return virtual;
}

uintptr_t *get_page(void *src) {
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

  return &directory->pml4t->pdpt[pml4t_index]
              ->pdt[pdpt_index]
              ->pt[pdt_index]
              ->page[pt_index];
}

void mmu_unmap_frames(void *src, size_t length) {
  uintptr_t p = (uintptr_t)src;
  for (size_t i = 0; i < length; i += PAGE_SIZE) {
    uintptr_t *page = get_page((void *)(p + i));
    assert(page);
    *page = (uintptr_t)NULL;
  }
  // FIXME: Possibly expensive operation that may be best to avoid
  // if unmap_frames is called multiple times.
  flush_tlb();
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

  struct mmu_directory *directory = mmu_get_active_directory();

  if (!(directory->pml4t->physical[pml4t_index] & PAGE_FLAG_PRESENT)) {
    return NULL;
  }
  if (!(directory->pml4t->pdpt[pml4t_index]->physical[pdpt_index] &
        PAGE_FLAG_PRESENT)) {
    return NULL;
  }
  if (!(directory->pml4t->pdpt[pml4t_index]
            ->pdt[pdpt_index]
            ->physical[pdt_index] &
        PAGE_FLAG_PRESENT)) {
    return NULL;
  }
  if (!(directory->pml4t->physical[pml4t_index] & PAGE_FLAG_PRESENT)) {
    return NULL;
  }

  uintptr_t p = directory->pml4t->pdpt[pml4t_index]
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
  struct mmu_directory *directory = mmu_get_active_directory();

  // kprintf("pml4t_index: %d\n", pml4t_index);
  // kprintf("directory: %x\n", directory);
  if (!(directory->pml4t->physical[pml4t_index] & PAGE_FLAG_PRESENT)) {
    void *physical;
    struct PDPT *pdpt = safe_allocation(sizeof(struct PDPT), &physical);
    directory->pml4t->physical[pml4t_index] = (uintptr_t)physical | 0x3;
    directory->pml4t->pdpt[pml4t_index] = pdpt;
  }

  if (!(directory->pml4t->pdpt[pml4t_index]->physical[pdpt_index] &
        PAGE_FLAG_PRESENT)) {
    void *physical;
    struct PDT *pdt = safe_allocation(sizeof(struct PDT), &physical);
    directory->pml4t->pdpt[pml4t_index]->physical[pdpt_index] =
        (uintptr_t)physical | 0x3;
    directory->pml4t->pdpt[pml4t_index]->pdt[pdpt_index] = pdt;
  }

  if ((directory->pml4t->pdpt[pml4t_index]
           ->pdt[pdpt_index]
           ->physical[pdt_index] &
       PAGE_FLAG_PRESENT)) {
    return false;
  }

  void *physical;
  void *address = safe_allocation(sizeof(struct PT), &physical);

  directory->pml4t->pdpt[pml4t_index]->pdt[pdpt_index]->physical[pdt_index] =
      (uintptr_t)physical | 0x3;
  directory->pml4t->pdpt[pml4t_index]->pdt[pdpt_index]->pt[pdt_index] = address;

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

  struct mmu_directory *directory = mmu_get_active_directory();
  void **p = (void **)&directory->pml4t->pdpt[pml4t_index]
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
  void *new_stack = (void *)0xffffff8000000000;

  size_t stack_size = 0x8000;

  for (size_t i = 0x1000; i < stack_size; i += PAGE_SIZE) {
    assert(check_virtual_region_is_free((void *)((uintptr_t)new_stack - i),
                                        NULL, true, false, NULL));
  }

  goto_function_with_stack(function, new_stack);
}

void copy_frame(void *physical_dst, void *physical_src) {
  /*
  if (NULL == (void *)((uintptr_t)physical_src & (~0xFFF))) {
    return;
  }
  */
  void *dst =
      mmu_map_frames((void *)((uintptr_t)physical_dst & (~0xFFF)), PAGE_SIZE);
  void *src =
      mmu_map_frames((void *)((uintptr_t)physical_src & (~0xFFF)), PAGE_SIZE);

  assert(((uintptr_t)physical_dst & (~0xFFF)) ==
         ((uintptr_t)mmu_virtual_to_physical(dst, NULL)));
  //  kprintf("Physical src: %x\n", physical_src);
  //  kprintf("Virt to physical: %x\n", mmu_virtual_to_physical(src, NULL));
  assert(((uintptr_t)physical_src & (~0xFFF)) ==
         ((uintptr_t)mmu_virtual_to_physical(src, NULL)));

  memcpy(dst, src, PAGE_SIZE);

  mmu_unmap_frames(dst, PAGE_SIZE);
  mmu_unmap_frames(src, PAGE_SIZE);
}

bool clone_pt(struct PT *orig_pt, struct PT **new_pt, void **physical) {
  *new_pt = safe_allocation(sizeof(struct PT), physical);

  for (int i = 0; i < 512; i++) {
    int flags = orig_pt->page[i] & 0xFFF;
    if (!(flags & PAGE_FLAG_PRESENT)) {
      continue;
    }

    (*new_pt)->page[i] = (uintptr_t)get_frame(true, 1) | flags;
    copy_frame((void *)((*new_pt)->page[i] & ~0xFFF), (void *)orig_pt->page[i]);
  }

  return true;
}

bool clone_pdt(struct PDT *orig_pdt, struct PDT **new_pdt, void **physical) {
  *new_pdt = safe_allocation(sizeof(struct PDT), physical);

  for (int i = 0; i < 512; i++) {
    int flags = orig_pdt->physical[i] & 0xFFF;
    if (!(flags & PAGE_FLAG_PRESENT)) {
      continue;
    }
    assert(clone_pt(orig_pdt->pt[i], &((*new_pdt)->pt[i]),
                    (void **)&((*new_pdt)->physical[i])));
    (*new_pdt)->physical[i] |= flags;
  }

  return true;
}

bool clone_pdpt(struct PDPT *orig_pdpt, struct PDPT **new_pdpt,
                void **physical) {
  *new_pdpt = safe_allocation(sizeof(struct PDPT), physical);

  for (int i = 0; i < 512; i++) {
    int flags = orig_pdpt->physical[i];
    if (!(flags & PAGE_FLAG_PRESENT)) {
      continue;
    }
    assert(clone_pdt(orig_pdpt->pdt[i], &((*new_pdpt)->pdt[i]),
                     (void **)&((*new_pdpt)->physical[i])));
    (*new_pdpt)->physical[i] |= flags;
  }

  return true;
}

struct mmu_directory *mmu_clone_directory(struct mmu_directory *directory) {
  struct mmu_directory *new_mmu_directory = ksbrk(sizeof(struct mmu_directory));

  void *physical;
  struct PML4T *pml4t = safe_allocation(sizeof(struct PML4T), &physical);
  new_mmu_directory->pml4t = pml4t;
  new_mmu_directory->physical = physical;

  for (int i = 0; i < 511; i++) {
    if (active_bootstrap && 0 == i) {
      continue;
    }
    int flags = directory->pml4t->physical[i] & 0xFFF;
    if (!(flags & PAGE_FLAG_PRESENT)) {
      continue;
    }
    assert(clone_pdpt(directory->pml4t->pdpt[i], &pml4t->pdpt[i],
                      (void **)&pml4t->physical[i]));
    pml4t->physical[i] |= flags;
  }

  new_mmu_directory->pml4t->pdpt[511] = directory->pml4t->pdpt[511];
  new_mmu_directory->pml4t->physical[511] = directory->pml4t->physical[511];

  return new_mmu_directory;
}

// TODO: Put this in a header
void set_cr3(void *cr3);

void mmu_set_directory(struct mmu_directory *directory) {
  kernel_threads[core_id_get()].active_directory = directory;
  set_cr3(directory->physical);
}

void mmu_remove_identity(void) {
  struct mmu_directory *directory = mmu_get_active_directory();
  directory->pml4t->pdpt[0] = NULL;
  directory->pml4t->physical[0] = (uintptr_t)NULL;
}

struct mmu_directory *mmu_get_active_directory(void) {
  return kernel_threads[core_id_get()].active_directory;
}

void set_stack_and_jump(void *, void *);
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

  struct mmu_directory *new_directory = mmu_clone_directory(base_directory);
  if (!new_directory) {
    assert(0);
    return;
  }

  new_directory->pml4t->pdpt[0] = base_directory->pml4t->pdpt[0];
  new_directory->pml4t->physical[0] = base_directory->pml4t->physical[0];

  mmu_set_directory(new_directory);

  void *new_stack = (void *)0xffffff8000000000;
  set_stack_and_jump(new_stack, main);
}

int mmu_init(void *multiboot_header) {
  struct mmu_directory *active_directory = &orig_active_directory;
  kernel_threads[core_id_get()].active_directory = active_directory;

  active_directory->pml4t =
      (struct PML4T *)(((uintptr_t)&PML4T) + 0xffffff8000000000);
  active_directory->physical = &PML4T;

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
    uintptr_t p = active_directory->pml4t->physical[i] + 0xFFFFFF8000000000;
    if (!(p & PAGE_FLAG_PRESENT)) {
      continue;
    }

    struct PDPT *pdpt = (struct PDPT *)(p & ~(0xFFF));
    active_directory->pml4t->pdpt[i] = pdpt;

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

  ksbrk(0x0);

  flush_tlb();
  return 1;
}
