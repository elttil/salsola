#include <assert.h>
#include <lock.h>
#include <string.h>

void rwlock_init(rwlock_t *lock) {
  memset(lock, 0, sizeof(rwlock_t));
}

void rwlock_read_acquire(rwlock_t *lock) {
  lock_acquire(&lock->r);
  lock->b++;
  if (1 == lock->b) {
    lock_acquire(&lock->g);
  }
  lock_release(&lock->r);
}

void rwlock_read_release(rwlock_t *lock) {
  lock_acquire(&lock->r);
  assert(0 != lock->b);
  lock->b--;
  if (0 == lock->b) {
    lock_release(&lock->g);
  }
  lock_release(&lock->r);
}

void rwlock_write_acquire(rwlock_t *lock) {
  lock_acquire(&lock->g);
}

void rwlock_write_release(rwlock_t *lock) {
  lock_release(&lock->g);
}
