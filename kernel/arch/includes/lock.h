#include <typedefs.h>

typedef u32 lock_t;

void lock_acquire(lock_t *lock);
void lock_release(lock_t *lock);
