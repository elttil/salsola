#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

err_t sa_write(int fd, const void *buf, u64 count, u64 *out) {
  return syscall(SYS_WRITE, fd, (u64)buf, count, (u64)out, 0);
}

ssize_t write(int fd, const void *buf, size_t count) {
  u64 out;
  err_t error = sa_write(fd, buf, count, &out);
  if (ERROR_SUCCESS != error) {
    errno = error_to_errno(error);
    return -1;
  }
  return out;
}
