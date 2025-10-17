#include <typedefs.h>

void csprng_init(void);
void csprng_get_random(void *buffer, u64 len);
void csprng_add_entropy(void *buffer, u64 size);
void csprng_add_entropy_fast(void *buffer, u64 size);
