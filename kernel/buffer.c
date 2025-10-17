#include <buffer.h>
#include <string.h>

#define min(a, b) (((a) < (b)) ? (a) : (b))

void buffer_init(struct buffer *ctx, void *buffer, size_t size) {
  ctx->buf = buffer;
  ctx->size = size;
}

err_t buffer_write(struct buffer *ctx, const void *buffer, u64 count,
                   u64 offset, u64 *out) {
  if (offset >= ctx->size) {
    ASSIGN_PTR(out, 0);
    return ERROR_WRITE_EXCEEDS_BOUNDS;
  }

  size_t left = ctx->size - offset;
  count = min(left, count);
  memcpy(((u8 *)ctx->buf) + offset, buffer, count);
  ASSIGN_PTR(out, count);
  return ERROR_SUCCESS;
}

err_t buffer_read(struct buffer *ctx, void *buffer, u64 count, u64 offset,
                  u64 *out) {
  if (offset >= ctx->size) {
    ASSIGN_PTR(out, 0);
    return ERROR_READ_EXCEEDS_BOUNDS;
  }

  size_t left = ctx->size - offset;
  count = min(left, count);
  memcpy(buffer, ((u8 *)ctx->buf) + offset, count);
  ASSIGN_PTR(out, count);
  return ERROR_SUCCESS;
}
