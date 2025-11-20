#ifndef BUFFER_H
#define BUFFER_H
#include <error.h>
#include <stddef.h>
#include <typedefs.h>

struct buffer {
  void *buf;
  u64 size;
};

void buffer_init(struct buffer *ctx, void *buffer, size_t size);
err_t buffer_write(struct buffer *ctx, const void *buffer, u64 count,
                   u64 offset, size_t *out);
err_t buffer_read(struct buffer *ctx, void *buffer, u64 count, u64 offset,
                  size_t *out);
#endif // BUFFER_H
