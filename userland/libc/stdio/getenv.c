#include <error.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tb/sv.h>
#include <unistd.h>

size_t buffer_count = 0;
size_t buffer_capacity = 0;
char *buffer = NULL;

static inline void reset_buffer(void) {
  free(buffer);
  buffer_capacity = 0;
  buffer_count = 0;
  buffer = NULL;
}

static bool buffer_append(const char *str, size_t length) {
  if (length + buffer_count >= buffer_capacity) {
    buffer_capacity += 512;
    void *out = realloc(buffer, buffer_capacity);
    if (!out) {
      return false;
    }
    buffer = out;
  }
  memcpy(buffer + buffer_count, str, length);
  buffer_count += length;
  return true;
}

char *getenv(const char *name) {
  char path[256];
  int rc = snprintf(path, sizeof(path), "/env/%s", name);
  struct sv sv_path = sv_init(path, rc);

  int fd;
  err_t err = sa_open(&fd, sv_path, O_READ, 0);
  if (ERROR_SUCCESS != err) {
    return NULL;
  }

  for (;;) {
    u64 rc;
    char tmp[256];
    if (ERROR_SUCCESS != sa_read(fd, tmp, sizeof(tmp), &rc)) {
      reset_buffer();
      close(fd);
      return NULL;
    }
    if (0 == rc) {
      break;
    }
    if (!buffer_append(tmp, rc)) {
      reset_buffer();
      close(fd);
      return NULL;
    }
  }
  close(fd);

  char nul = '\0';
  if (!buffer_append(&nul, 1)) {
    reset_buffer();
    return NULL;
  }
  return buffer;
}
