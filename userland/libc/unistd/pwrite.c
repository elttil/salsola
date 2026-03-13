#include <stdio.h>
#include <unistd.h>

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
  u64 out;
  err_t error = sa_pwrite(fd, buf, count, offset, &out);
  if (ERROR_SUCCESS != error) {
    errno = error_to_errno(error);
    return -1;
  }
  return out;
}
