#include <typedefs.h>

typedef volatile u32 lock_t;

int lock_try(lock_t *lock);
void lock_acquire(lock_t *lock);
void lock_release(lock_t *lock);
