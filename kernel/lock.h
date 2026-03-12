#ifndef LOCK_H
#define LOCK_H
#include <spinlock.h>
#include <typedefs.h>

typedef struct {
  lock_t r;
  lock_t g;
  volatile u32 b;
} rwlock_t;
#endif // LOCK_H

void rwlock_init(rwlock_t *lock);
void rwlock_read_acquire(rwlock_t *lock);
void rwlock_read_release(rwlock_t *lock);
void rwlock_write_acquire(rwlock_t *lock);
void rwlock_write_release(rwlock_t *lock);
