#include <stdio.h>
#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
  u64 out;
  err_t error = sa_pread(fd, buf, count, offset, &out);
  if (ERROR_SUCCESS != error) {
    errno = error_to_errno(error);
    return -1;
  }
  return out;
}
