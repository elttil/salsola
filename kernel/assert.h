#define assert(expr)                                                           \
  {                                                                            \
    if (!(expr)) {                                                             \
      aFailed(__FILE__, __LINE__);                                             \
      for (;;)                                                                 \
        ;                                                                      \
    }                                                                          \
  }

#define ASSERT_BUT_FIXME_PROPOGATE(expr) assert(expr)

void aFailed(char *f, int l);
