#ifndef SYS_RAND_H
#define SYS_RAND_H

#include <stdbool.h>
#include <stdint.h>
#include <typedefs.h>
#include <stddef.h>

void randomfill(void *buffer, uint32_t size);

struct rng_ctx {
  u32 block[16];
  bool aggressive_key_overwriting;
};

void sarandom_init(struct rng_ctx *ctx);
void sarandom_set_aggressive_key_overwriting(struct rng_ctx *ctx);
void sarandom_buf(struct rng_ctx *ctx, void *buffer, size_t size);
#endif // SYS_RAND_H
