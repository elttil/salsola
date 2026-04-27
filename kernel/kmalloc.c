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

// #define KMALLOC_DEBUG

#define IS_FREE (1 << 0)
#define IS_FINAL (1 << 1)

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

typedef struct MallocHeader {
  u64 magic;
  u32 size;
  u8 flags;
  struct MallocHeader *n;
} MallocHeader;

u64 delta_page(u64 a) {
  return 0x1000 - (a % 0x1000);
}

MallocHeader *head = NULL;
MallocHeader *final = NULL;
u32 total_heap_size = 0;

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
  head = (MallocHeader *)ksbrk(NEW_ALLOC_SIZE);
  if (!head) {
    return 0;
  }
  total_heap_size += NEW_ALLOC_SIZE - sizeof(MallocHeader);
  head->magic = 0xdde51ab9410268b1;
  head->size = NEW_ALLOC_SIZE - sizeof(MallocHeader);
  head->flags = IS_FREE | IS_FINAL;
  head->n = NULL;
  final = head;

#ifndef NO_SLAB
  for (size_t i = 0; i < ARRAY_LEN(slabs); i++) {
    slab_init(&slabs[i]);
  }
#endif // NO_SLAB
  return 1;
}

int add_heap_memory(size_t min_desired) {
  min_desired += sizeof(MallocHeader);
  size_t allocation_size = max(min_desired, NEW_ALLOC_SIZE);
  allocation_size += delta_page(allocation_size);
  allocation_size += NEW_ALLOC_SIZE;
  void *p;
  if (!(p = ksbrk(allocation_size))) {
    return 0;
  }
  total_heap_size += allocation_size - sizeof(MallocHeader);
  if (IS_FREE & final->flags) {
    void *e = final;
    e = (void *)((uintptr_t)e + sizeof(MallocHeader) + final->size);
    if (p == e) {
      final->size += allocation_size - sizeof(MallocHeader);
      return 1;
    }
  }
  MallocHeader *new_entry = p;
  new_entry->size = allocation_size - sizeof(MallocHeader);
  new_entry->flags = IS_FREE | IS_FINAL;
  new_entry->n = NULL;
  new_entry->magic = 0xdde51ab9410268b1;
  final->n = new_entry;
  final = new_entry;
  return 1;
}

static MallocHeader *next_header(MallocHeader *a) {
  assert(a->magic == 0xdde51ab9410268b1);
  if (a->n) {
    if (a->n->magic != 0xdde51ab9410268b1) {
      kprintf("Real magic value is: %x\n", a->n->magic);
      kprintf("location: %x\n", &(a->n->magic));
      assert(0);
    }
    return a->n;
  }
  return NULL;
}

void kmalloc_scan(void) {
  lock_acquire(&heap_lock);
  if (!head) {
    lock_release(&heap_lock);
    return;
  }
  MallocHeader *p = head;
  for (; (p = next_header(p));)
    ;
  lock_release(&heap_lock);
}

static MallocHeader *next_close_header(MallocHeader *a) {
  assert(a);
  if (a->flags & IS_FINAL) {
    return NULL;
  }
  return next_header(a);
}

int merge_headers(MallocHeader *b);

static MallocHeader *find_free_entry(u32 s) {
  // A new header is required as well as the newly allocated chunk
  s += sizeof(MallocHeader);
  if (!head) {
    if (!kmalloc_init()) {
      return NULL;
    }
  }
  MallocHeader *p = head;
  for (; p; p = next_header(p)) {
    assert(p->magic == 0xdde51ab9410268b1);
    if (!(p->flags & IS_FREE)) {
      continue;
    }
    u64 required_size = s;
    if (p->size < required_size) {
      for (; merge_headers(p);)
        ;
      if (p->size >= required_size) {
        return p;
      }
      continue;
    }
    return p;
  }
  return NULL;
}

int merge_headers(MallocHeader *b) {
  if (!(b->flags & IS_FREE)) {
    return 0;
  }

  MallocHeader *n = next_close_header(b);
  if (!n) {
    return 0;
  }

  if (!(n->flags & IS_FREE)) {
    return 0;
  }

  b->size += n->size;
  b->flags |= n->flags & IS_FINAL;
  b->n = n->n;
  if (n == final) {
    final = b;
  }
  return 1;
}

#ifdef KMALLOC_DEBUG
void *int_kmalloc(size_t s) {
  interrupts_disable();
  u8 *rc = kmalloc_align(s, NULL);
  prng_get_pseudorandom(rc, s);
  rc += align_page(s);
  rc -= s;

  void *delay = kmalloc_align(1, NULL);
  kmalloc_align_free(delay, 1);
  return (void *)rc;
}

void kfree(void *p) {
  get_fast_insecure_random(align_page(p) - 0x1000, 0x1000);
  kmalloc_align_free(p, 0x1000);
}
#else

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

  size_t n = s;
  MallocHeader *free_entry = find_free_entry(s);
  if (!free_entry) {
    if (!add_heap_memory(s)) {
      //      klog(LOG_ERROR, "Ran out of memory.");
      lock_release(&heap_lock);
      return NULL;
    }
    lock_release(&heap_lock);
    return kmalloc(s);
  }

  void *rc = (void *)(free_entry + 1);

  // Create a new header
  MallocHeader *new_entry = (MallocHeader *)((uintptr_t)rc + n);
  new_entry->flags = free_entry->flags;
  new_entry->n = free_entry->n;
  new_entry->size = free_entry->size - n - sizeof(MallocHeader);
  new_entry->magic = 0xdde51ab9410268b1;

  if (free_entry == final) {
    final = new_entry;
  }
  merge_headers(new_entry);

  // Modify the free entry
  free_entry->size = n;
  free_entry->flags = 0;
  free_entry->n = new_entry;
  free_entry->magic = 0xdde51ab9410268b1;
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

  MallocHeader *h = (MallocHeader *)((uintptr_t)p - sizeof(MallocHeader));
  assert(h->magic == 0xdde51ab9410268b1);
  assert(!(h->flags & IS_FREE));

  prng_get_pseudorandom((void *)p, h->size);

  h->flags |= IS_FREE;
  merge_headers(h);
  lock_release(&heap_lock);
}
#endif // KMALLOC_DEBUG

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

  return ((MallocHeader *)((uintptr_t)ptr - sizeof(MallocHeader)))->size;
}

void *krealloc(void *ptr, size_t size) {
  if (!ptr) {
    return kmalloc(size);
  }
  size_t l = get_mem_size(ptr);
  /*
    size_t l = get_mem_size(ptr);
    if (l == size) {
      return ptr;
    }
    if (l > size) {
      MallocHeader *header = (MallocHeader *)((u8 *)ptr - sizeof(MallocHeader));
      header->size = size;
      return ptr;
    }
  */
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
