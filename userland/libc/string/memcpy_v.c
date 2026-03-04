#include <string.h>

volatile void *memcpy_v(volatile void *dest, const volatile void *src, uint32_t n) {
  volatile unsigned char *d = dest;
  const volatile unsigned char *s = src;

  for (; (0 != ((uintptr_t)s % 8)) && n; n--) {
    *d++ = *s++;
  }

  if ((uintptr_t)d % 8 == 0) {
    for (; n >= 8; n -= 8, d += 8, s += 8) {
      *(uint64_t *)d = *(uint64_t *)s;
    }
  }

  if ((uintptr_t)d % 4 == 0) {
    for (; n >= 4; n -= 4, d += 4, s += 4) {
      *(uint32_t *)d = *(uint32_t *)s;
    }
  }

  if ((uintptr_t)d % 2 == 0) {
    for (; n >= 2; n -= 2, d += 2, s += 2) {
      *(uint16_t *)d = *(uint16_t *)s;
    }
  }
  for (; n; n--) {
    *d++ = *s++;
  }
  return dest;
}
