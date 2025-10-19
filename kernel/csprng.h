#include <stdbool.h>
#include <sv.h>
#include <typedefs.h>

void csprng_init(void);
void csprng_get_random(void *buffer, u64 len);
void csprng_add_entropy(const void *buffer, u64 size);
void csprng_add_entropy_fast(void *buffer, u64 size);
bool csprng_add_random_device(struct sv filename);
u64 csprng_get_uniform(u64 upperbound);
