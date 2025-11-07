#include <assert.h>
#include <string.h>
#include <sys/random.h>
#include <typedefs.h>

// TODO: Move crypto to a different file

#define KEY 4
#define KEY_SIZE 8 * sizeof(u32)
#define COUNT 12
#define COUNT_SIZE sizeof(u32)
#define COUNT_MAX (0x100000000 - 1) // 2^32 - 1
#define NONCE 13
#define NONCE_SIZE 2 * sizeof(u32)
#define BLOCK_SIZE 16 * sizeof(u32)

#define ROTL(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define QR(a, b, c, d)                                                         \
  (a += b, d ^= a, d = ROTL(d, 16), c += d, b ^= c, b = ROTL(b, 12), a += b,   \
   d ^= a, d = ROTL(d, 8), c += d, b ^= c, b = ROTL(b, 7))
#define ROUNDS 20

static void chacha_block(u32 out[16], u32 const in[16]) {
  u32 x[16];

  for (int i = 0; i < 16; ++i) {
    x[i] = in[i];
  }
  for (int i = 0; i < ROUNDS; i += 2) {
    QR(x[0], x[4], x[8], x[12]);
    QR(x[1], x[5], x[9], x[13]);
    QR(x[2], x[6], x[10], x[14]);
    QR(x[3], x[7], x[11], x[15]);

    QR(x[0], x[5], x[10], x[15]);
    QR(x[1], x[6], x[11], x[12]);
    QR(x[2], x[7], x[8], x[13]);
    QR(x[3], x[4], x[9], x[14]);
  }
  for (int i = 0; i < 16; ++i) {
    out[i] = x[i] + in[i];
  }
}

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

struct rng_ctx global_rng_ctx;

void sarandom_init(struct rng_ctx *ctx) {
  if (!ctx) {
    memcpy(global_rng_ctx.block, internal_chacha_block,
           sizeof(internal_chacha_block));
    randomfill(&global_rng_ctx.block[KEY], KEY_SIZE);
    return;
  }
  memcpy(ctx->block, internal_chacha_block, sizeof(internal_chacha_block));
  sarandom_buf(&global_rng_ctx, &ctx->block[KEY], KEY_SIZE);
}

void sarandom_set_aggressive_key_overwriting(struct rng_ctx *ctx) {
  if (!ctx) {
    ctx = &global_rng_ctx;
  }
  ctx->aggressive_key_overwriting = true;
}

static void chacha_new_key(struct rng_ctx *ctx) {
  u32 generated[16];
  chacha_block(generated, ctx->block);
  memcpy(&ctx->block[KEY], generated, KEY_SIZE);
  ctx->block[COUNT] = 0;
}

static void chacha_mix_block(struct rng_ctx *ctx, u32 out[16]) {
  if (COUNT_MAX - 1 == ctx->block[COUNT]) {
    // The current block has used up all the 2^32 counts. If the
    // key and/or the nonce are not changed and the count
    // overflows back to zero then the random values would
    // repeate. This is of course not desiered behaviour. The
    // solution is to create a new nonce and key using the
    // already established chacha block.
    chacha_new_key(ctx);
  }
  chacha_block(out, ctx->block);
  ctx->block[COUNT]++;
}

void sarandom_buf(struct rng_ctx *ctx, void *buffer, size_t size) {
  if (!ctx) {
    ctx = &global_rng_ctx;
  }
  u32 generated[16];
  u8 *ptr = buffer;
  for (; size > 0;) {
    chacha_mix_block(ctx, generated);

    u32 read_len = (BLOCK_SIZE < size) ? (BLOCK_SIZE) : size;
    memcpy(ptr, generated, read_len);
    ptr += read_len;
    size -= read_len;
  }
  if (ctx->aggressive_key_overwriting) {
    chacha_new_key(ctx);
  }
}
