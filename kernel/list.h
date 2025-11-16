#include <kmalloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <typedefs.h>

#define DEFINE_LIST_STRUCT(prefix, type)                                       \
  struct prefix##_ctx {                                                        \
    type *items;                                                               \
    uint64_t length;                                                           \
    uint64_t capacity;                                                         \
    uint64_t entries;                                                          \
  };                                                                           \
  void prefix##_init(struct prefix##_ctx *ctx);                                \
  void prefix##_free(struct prefix##_ctx *ctx);                                \
  bool prefix##_add(struct prefix##_ctx *ctx, type value, u64 *index);         \
  bool prefix##_add_or_replace_previous_null(struct prefix##_ctx *ctx,         \
                                             type value, u64 *index);          \
  bool prefix##_get(const struct prefix##_ctx *ctx, u64 index, type *value);   \
  bool prefix##_clone(struct prefix##_ctx *ctx,                                \
                      const struct prefix##_ctx *source);                      \
  bool prefix##_find_index_by_value(struct prefix##_ctx *ctx, u64 *index,      \
                                    type value);                               \
  uint64_t prefix##_num_entries(struct prefix##_ctx *ctx);\
  bool prefix##_set(struct prefix##_ctx *ctx, u64 index, type value);

#define DEFINE_LIST_FUNCTIONS(prefix, type)                                    \
  void prefix##_init(struct prefix##_ctx *ctx) {                               \
    ctx->items = NULL;                                                         \
    ctx->length = 0;                                                           \
    ctx->entries = 0;                                                          \
    ctx->capacity = 128;                                                       \
    ctx->items = krecalloc(ctx->items, ctx->capacity, sizeof(type));           \
    if (!ctx->items) {                                                         \
      ctx->capacity = 0;                                                       \
    }                                                                          \
  }                                                                            \
                                                                               \
  void prefix##_free(struct prefix##_ctx *ctx) {                               \
    kfree(ctx->items);                                                         \
    ctx->items = NULL;                                                         \
    ctx->length = 0;                                                           \
    ctx->capacity = 0;                                                         \
    ctx->entries = 0;                                                          \
  }                                                                            \
                                                                               \
  static bool prefix##_increase_size(struct prefix##_ctx *ctx, u64 increase) { \
    u64 new_capacity = ctx->capacity + increase;                               \
    void *new_allocation = krecalloc(ctx->items, new_capacity, sizeof(type));  \
    if (!new_allocation) {                                                     \
      return false;                                                            \
    }                                                                          \
    ctx->items = new_allocation;                                               \
    ctx->capacity += new_capacity;                                             \
    return true;                                                               \
  }                                                                            \
                                                                               \
  bool prefix##_add(struct prefix##_ctx *ctx, type value, u64 *index) {        \
    if (ctx->length == ctx->capacity) {                                        \
      if (!prefix##_increase_size(ctx, 128)) {                                 \
        return false;                                                          \
      }                                                                        \
    }                                                                          \
    memcpy(ctx->items + ctx->length, &value, sizeof(type));                    \
    if (index) {                                                               \
      *index = ctx->length;                                                    \
    }                                                                          \
    ctx->length++;                                                             \
    ctx->entries++;                                                            \
    return true;                                                               \
  }                                                                            \
                                                                               \
  bool prefix##_get(const struct prefix##_ctx *ctx, u64 index, type *value) {  \
    if (index >= ctx->length) {                                                \
      return false;                                                            \
    }                                                                          \
    if (value) {                                                               \
      memcpy(value, ctx->items + index, sizeof(type));                         \
    }                                                                          \
    return true;                                                               \
  }                                                                            \
                                                                               \
  bool prefix##_clone(struct prefix##_ctx *ctx,                                \
                      const struct prefix##_ctx *source) {                     \
    prefix##_init(ctx);                                                        \
    type value;                                                                \
    for (u64 i = 0; i < source->length; i++) {                                 \
      assert(prefix##_get(source, i, &value));                                 \
      /* TODO: Handle OOM */                                                   \
      assert(prefix##_add(ctx, value, NULL));                                  \
    }                                                                          \
    return true;                                                               \
  }                                                                            \
                                                                               \
  uint64_t prefix##_num_entries(struct prefix##_ctx *ctx) {                    \
    return ctx->entries;                                                       \
  }                                                                            \
                                                                               \
  bool prefix##_find_index_by_value(struct prefix##_ctx *ctx, u64 *index,      \
                                    type value) {                              \
    for (u64 i = 0; i < ctx->length; i++) {                                    \
      if (*(ctx->items + i) == value) {                                        \
        if (index) {                                                           \
          *index = i;                                                          \
        }                                                                      \
        return true;                                                           \
      }                                                                        \
    }                                                                          \
    return false;                                                              \
  }                                                                            \
                                                                               \
  bool prefix##_add_or_replace_previous_null(struct prefix##_ctx *ctx,         \
                                             type value, u64 *index) {         \
    for (u64 i = 0; i < ctx->length; i++) {                                    \
      if (*(ctx->items + i) == NULL) {                                         \
        if (index) {                                                           \
          *index = i;                                                          \
        }                                                                      \
        ctx->entries++;                                                        \
        return true;                                                           \
      }                                                                        \
    }                                                                          \
    return prefix##_add(ctx, value, index);                                    \
  }                                                                            \
                                                                               \
  void prefix##_remove(struct prefix##_ctx *ctx, u64 index) {                  \
    if (index >= ctx->length) {                                                \
      return;                                                                  \
    }                                                                          \
    assert(ctx->entries != 0);                                                 \
    ctx->entries--;                                                            \
    memset(ctx->items + index, 0, sizeof(type));                               \
  }                                                                            \
                                                                               \
  bool prefix##_set(struct prefix##_ctx *ctx, u64 index, type value) {         \
    if (index >= ctx->length) {                                                \
      if (!prefix##_increase_size(ctx, ctx->length - index + 128)) {           \
        return false;                                                          \
      }                                                                        \
    }                                                                          \
    memcpy(ctx->items + index, &value, sizeof(type));                          \
    return true;                                                               \
  }
