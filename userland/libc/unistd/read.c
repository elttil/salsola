#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

err_t sa_read(int fd, void *buf, u64 count, u64 *out) {
  return syscall(SYS_READ, fd, (u64)buf, count, (u64)out, 0);
}

ssize_t read(int fd, void *buf, size_t count) {
  u64 out;
  err_t error = sa_read(fd, buf, count, &out);
  if (ERROR_SUCCESS != error) {
    errno = error_to_errno(error);
    return -1;
  }
  return out;
}
