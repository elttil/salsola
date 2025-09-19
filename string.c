#include <string.h>
#include <typedefs.h>

size_t strlen(const char *s) {
  const char *o = s;
  for (; *s; s++)
    ;
  return s - o;
}

void *memcpy(void *dest, const void *src, size_t n) {
  unsigned char *d = dest;
  const unsigned char *s = src;

  for (; n >= 8; n -= 8, d += 8, s += 8) {
    *(u64 *)d = *(u64 *)s;
  }

  for (; n >= 4; n -= 4, d += 4, s += 4) {
    *(u32 *)d = *(u32 *)s;
  }

  for (; n >= 2; n -= 2, d += 2, s += 2) {
    *(u16 *)d = *(u16 *)s;
  }

  for (; n; n--) {
    *d++ = *s++;
  }
  return dest;
}

void *memset(void *s, int c, size_t n) {
  char *p = s;
  for (; n > 0; n--) {
    *p = c;
  }
  return s;
}
