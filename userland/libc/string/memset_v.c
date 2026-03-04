#include <stddef.h>
#include <string.h>

volatile void *memset_v(volatile void *s, int c, size_t n) {
  volatile unsigned char *p = s;
  for (; n > 0; n--, p++) {
    *p = (unsigned char)c;
  }
  return s;
}
