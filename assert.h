#define assert(expr)                                                           \
  {                                                                            \
    if (!(expr)) {                                                             \
      aFailed(__FILE__, __LINE__);                                             \
      for (;;)                                                                 \
        ;                                                                      \
    }                                                                          \
  }

void aFailed(char *f, int l);
