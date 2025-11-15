#include <kmalloc.h>
#include <string.h>
#include <typedefs.h>

size_t strlen(const char *s) {
  const char *o = s;
  for (; *s; s++)
    ;
  return s - o;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  // The strcmp() function shall compare the string pointed to by s1
  // to the string pointed to by s2.
  int l1, l2, rc;
  l1 = l2 = rc = 0;
  for (; (*s1 || *s2) && n > 0; n--) {
    if (*s1 != *s2) {
      rc++;
    }
    if (*s1) {
      l1++;
      s1++;
    }
    if (*s2) {
      l2++;
      s2++;
    }
  }

  // Upon completion, strcmp() shall return an integer greater than,
  // equal to, or less than 0, if the string pointed to by s1 is
  // greater than, equal to, or less than the string pointed to by
  // s2, respectively.
  if (l2 > l1) {
    return -rc;
  }
  return rc;
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
  for (; n > 0; n--, p++) {
    *p = c;
  }
  return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  int return_value = 0;

  for (uint32_t i = 0; i < n; i++) {
    if (((unsigned char *)(s1))[i] != ((unsigned char *)(s2))[i]) {
      return_value++;
    }
  }

  return return_value;
}

void *memmove(void *s1, const void *s2, size_t n) {
  if (0 == n) {
    return s1;
  }
  // FIXME: This is bad
  char *tmp = kmalloc(n);
  memcpy(tmp, s2, n);
  memcpy(s1, tmp, n);
  kfree(tmp);
  return s1;
}
