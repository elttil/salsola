// FIXME: This is mostlikely incredibly inefficent and insecure.
#include <assert.h>
#include <crypto/ChaCha20/chacha20.h>
#include <crypto/SHA1/sha1.h>
#include <csprng.h>
#include <stddef.h>
#include <string.h>

#define HASH_CTX SHA1_CTX
#define HASH_LEN SHA1_LEN

u32 internal_chacha_block[16] = {
    // Constant ascii values of "expand 32-byte k"
    0x61707865,
    0x3320646e,
    0x79622d32,
    0x6b206574,
    // The unique key
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    // Block counter
    0x00000000,
    // Nonce
    0x00000000,
    0x00000000,
};

void mix_chacha(void) {
  u8 rand_data[BLOCK_SIZE];
  csprng_get_random(rand_data, BLOCK_SIZE);
  memcpy(internal_chacha_block + KEY, rand_data, KEY_SIZE);
  memcpy(internal_chacha_block + NONCE, rand_data + KEY_SIZE, NONCE_SIZE);
  internal_chacha_block[COUNT] = 0;
}

void csprng_get_random(u8 *buffer, u64 len) {
  u8 rand_data[BLOCK_SIZE];
  for (; len > 0;) {
    if (COUNT_MAX - 1 == internal_chacha_block[COUNT]) {
      // The current block has used up all the 2^32 counts. If the
      // key and/or the nonce are not changed and the count
      // overflows back to zero then the random values would
      // repeate. This is of course not desiered behaviour. The
      // solution is to create a new nonce and key using the
      // already established chacha block.
      internal_chacha_block[COUNT]++;
      mix_chacha();
    }
    u32 read_len = (BLOCK_SIZE < len) ? (BLOCK_SIZE) : len;
    chacha_block((u32 *)rand_data, internal_chacha_block);
    internal_chacha_block[COUNT]++;
    memcpy(buffer, rand_data, read_len);
    buffer += read_len;
    len -= read_len;
  }
}

HASH_CTX hash_pool;
u32 hash_pool_size = 0;

void add_hash_pool(void) {
  u8 new_chacha_key[KEY_SIZE];
  csprng_get_random(new_chacha_key, KEY_SIZE);

  u8 hash_buffer[HASH_LEN];
  SHA1_Final(&hash_pool, hash_buffer);
  for (size_t i = 0; i < HASH_LEN; i++) {
    new_chacha_key[i % KEY_SIZE] ^= hash_buffer[i];
  }

  SHA1_Init(&hash_pool);
  SHA1_Update(&hash_pool, hash_buffer, HASH_LEN);

  u8 block[BLOCK_SIZE];
  csprng_get_random(block, BLOCK_SIZE);
  SHA1_Update(&hash_pool, block, BLOCK_SIZE);

  memcpy(internal_chacha_block + KEY, new_chacha_key, KEY_SIZE);

  mix_chacha();
}

u64 entropy_fast_state = 0;

static inline uint64_t xorshift64(void) {
  uint64_t x = entropy_fast_state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  return entropy_fast_state = x;
}

void csprng_add_entropy(u8 *buffer, u64 size) {
  SHA1_Update(&hash_pool, &entropy_fast_state, sizeof(entropy_fast_state));
  xorshift64();
  SHA1_Update(&hash_pool, buffer, size);
  hash_pool_size += size;
  if (hash_pool_size >= HASH_LEN * 2) {
    add_hash_pool();
  }
}

void csprng_add_entropy_fast(u8 *buffer, u64 size) {
  for (u64 i = 0; i < size; i++) {
    entropy_fast_state ^= ((u64)buffer[i] << (8 * (i % 8)));
    if (0 == i % 8) {
      xorshift64();
    }
  }
  xorshift64();
}

void csprng_init(void) {
  SHA1_Init(&hash_pool);
  return;
}
