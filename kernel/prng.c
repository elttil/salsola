#include <crypto/xoshiro256plusplus/xoshiro256plusplus.h>
#include <csprng.h>
#include <prng.h>

uint64_t seed[4];

void prng_init(void) {
  csprng_get_random((u8 *)&seed, sizeof(seed));
}

void prng_get_pseudorandom(u8 *buffer, u64 len) {
  for (; len >= 8; len -= 8, buffer += 8) {
    *((uint64_t *)buffer) = xoshiro_256_pp(seed);
  }
  for (; len > 0; len--, buffer++) {
    *((uint8_t *)buffer) = xoshiro_256_pp(seed) & 0xFF;
  }
}
