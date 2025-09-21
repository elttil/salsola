#include <typedefs.h>

void csprng_init(void);
void csprng_get_random(u8 *buffer, u64 len);
void csprng_add_entropy(u8 *buffer, u64 size);
void csprng_add_entropy_fast(u8 *buffer, u64 size);
