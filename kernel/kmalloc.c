#include <assert.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <lock.h>
#include <math.h>
#include <mmu.h>
#include <prng.h>
#include <stdint.h>
#include <string.h>

#define NEW_ALLOC_SIZE 0x8000

#define ARRAY_LEN(a) (sizeof(a)) / (sizeof((a)[0]))

lock_t heap_lock;

// #define NO_SLAB

// This is sqrt(SIZE_MAX+1), as s1*s2 <= SIZE_MAX
// if both s1 < MUL_NO_OVERFLOW and s2 < MUL_NO_OVERFLOW
#define MUL_NO_OVERFLOW ((size_t)1 << (sizeof(size_t) * 4))
static inline bool is_multiplication_safe(size_t a, size_t b) {
  if ((a >= MUL_NO_OVERFLOW || b >= MUL_NO_OVERFLOW) && a > 0 &&
      SIZE_MAX / a < b) {
    return false;
  }
  return true;
}

void *kmalloc_align(size_t s, void **physical) {
  // TODO: It should reuse virtual regions so that it does not run out
  // of address space.
  void *rc;
  if (!(rc = ksbrk_physical(s, physical, false))) {
    return NULL;
  }
  return rc;
}

void kmalloc_align_free(void *p, size_t s) {
  // TODO
  (void)p;
  (void)s;
}

#ifndef NO_SLAB
struct slab {
  u64 *bitmap;
  void *allocation;
  size_t object_size;
  size_t object_count;
};

#define SLAB(s) {.object_size = s}

struct slab slabs[] = {
    SLAB(16), SLAB(32), SLAB(64), SLAB(128), SLAB(160), SLAB(256),
};

void slab_init(struct slab *slab) {
  slab->object_count = 64 * 8;

  size_t s = slab->object_size * slab->object_count;
  slab->bitmap = kcalloc(slab->object_count / 64, sizeof(u64));
  if (!slab->bitmap) {
    return;
  }

  slab->allocation = kmalloc(s);
}

void *slab_alloc(struct slab *slab) {
  if (!slab->allocation) {
    return NULL;
  }
  const size_t bitmap_array_length = slab->object_count / 64;
  assert(0 == slab->object_count % 64);
  for (size_t i = 0; i < bitmap_array_length; i++) {
    if (~((u64)0) == slab->bitmap[i]) {
      continue;
    }

    for (size_t offset = 0; offset < 64; offset++) {
      if (slab->bitmap[i] & ((u64)1 << offset)) {
        continue;
      }
      slab->bitmap[i] |= ((u64)1 << offset);
      void *o = (void *)((i * 64 + offset) * slab->object_size);
      assert((uintptr_t)o <= slab->object_count * slab->object_size);
      void *rc = (void *)((uintptr_t)slab->allocation + (uintptr_t)o);
      return rc;
    }
  }
  // TODO: Increase the size instead.
  return NULL;
}

struct slab *find_slab(size_t s) {
  for (size_t i = 0; i < ARRAY_LEN(slabs); i++) {
    if (s <= slabs[i].object_size) {
      return &slabs[i];
    }
  }
  return NULL;
}
#endif // NO_SLAB

int kmalloc_init(void) {
  // TODO: This should not be here, but is required for virtual memory
  // initialization (so it really really shouldn't be here).
  ksbrk(0x1000);
#ifndef NO_SLAB
  for (size_t i = 0; i < ARRAY_LEN(slabs); i++) {
    slab_init(&slabs[i]);
  }
#endif // NO_SLAB
  return 1;
}

void dump_backtrace(u32 max_frames);
void *int_kmalloc(size_t s) {
  lock_acquire(&heap_lock);

#ifndef NO_SLAB
  struct slab *slab = find_slab(s);
  if (slab) {
    void *rc = slab_alloc(slab);
    if (rc) {
      lock_release(&heap_lock);
      return rc;
    }
  }
#endif // NO_SLAB
  void *rc = mmu_alloc_with_guardpage(s);
  lock_release(&heap_lock);
  return rc;
}

void kfree(void *p) {
  if (!p) {
    return;
  }

  lock_acquire(&heap_lock);

#ifndef NO_SLAB
  for (size_t i = 0; i < ARRAY_LEN(slabs); i++) {
    if (p >= slabs[i].allocation &&
        p <= slabs[i].allocation +
                 slabs[i].object_size * slabs[i].object_count) {
      size_t offset = p - slabs[i].allocation;
      assert(0 == offset % slabs[i].object_size);
      offset /= slabs[i].object_size;

      size_t array_index = offset / 64;
      size_t array_offset = offset % 64;
      slabs[i].bitmap[array_index] &= ~((u64)1 << array_offset);

      prng_get_pseudorandom((void *)p, slabs[i].object_size);
      lock_release(&heap_lock);
      return;
    }
  }
#endif // NO_SLAB
  size_t off = (uintptr_t)p % PAGE_SIZE;
  prng_get_pseudorandom((void *)p - off, mmu_get_guardpage_allocation_size(p));
  mmu_free_guardpage_allocation(p);
  lock_release(&heap_lock);
}

err_t kmalloc2(void **ptr, size_t s) {
  void *rc = int_kmalloc(s);
  if (NULL == rc) {
    return ERROR_NO_MEMORY;
  }
  prng_get_pseudorandom((void *)rc, s);
  ASSIGN_PTR(ptr, rc);
  return ERROR_SUCCESS;
}

void *kmalloc(size_t s) {
  void *rc;
  if (ERROR_SUCCESS != kmalloc2(&rc, s)) {
    return NULL;
  }
  return rc;
}

size_t get_mem_size(void *ptr) {
  if (!ptr) {
    return 0;
  }
#ifndef NO_SLAB
  for (size_t i = 0; i < ARRAY_LEN(slabs); i++) {
    if (ptr >= slabs[i].allocation &&
        ptr <= slabs[i].allocation +
                   slabs[i].object_size * slabs[i].object_count) {
      return slabs[i].object_size;
    }
  }
#endif // NO_SLAB
  return mmu_get_guardpage_allocation_size(ptr) - ((uintptr_t)ptr % PAGE_SIZE);
}

void *krealloc(void *ptr, size_t size) {
  if (!ptr) {
    return kmalloc(size);
  }
  size_t l = get_mem_size(ptr);
  if (l >= size) {
    return ptr;
  }

  void *rc = kmalloc(size);
  if (!rc) {
    return NULL;
  }
  size_t to_copy = min(l, size);
  memcpy(rc, ptr, to_copy);
  kfree(ptr);
  return rc;
}

void *kreallocarray(void *ptr, size_t nmemb, size_t size) {
  if (!is_multiplication_safe(nmemb, size)) {
    return NULL;
  }

  return krealloc(ptr, nmemb * size);
}

void *kallocarray(size_t nmemb, size_t size) {
  return kreallocarray(NULL, nmemb, size);
}

void *krecalloc(void *ptr, size_t nelem, size_t elsize) {
  if (!is_multiplication_safe(nelem, elsize)) {
    return NULL;
  }
  if (!ptr) {
    return kcalloc(nelem, elsize);
  }
  size_t new_size = nelem * elsize;
  if (new_size < get_mem_size(ptr)) {
    return ptr;
  }
  void *rc = int_kmalloc(new_size);
  if (!rc) {
    return NULL;
  }
  size_t l = get_mem_size(ptr);
  size_t to_copy = min(l, new_size);
  memset(rc, 0, new_size);
  memcpy(rc, ptr, to_copy);
  kfree(ptr);
  return rc;
}

void *kcalloc(size_t nelem, size_t elsize) {
  if (!is_multiplication_safe(nelem, elsize)) {
    return NULL;
  }
  void *rc = int_kmalloc(nelem * elsize);
  if (!rc) {
    return NULL;
  }
  memset(rc, 0, get_mem_size(rc));
  return rc;
}
