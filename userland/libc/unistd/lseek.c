#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

err_t sa_lseek(u64 fd, off_t offset, int whence, off_t *out) {
  return syscall(SYS_LSEEK, fd, offset, whence, (u64)out, 0);
}

off_t lseek(int fd, off_t offset, int whence) {
  off_t r;
  err_t err = sa_lseek(fd, offset, whence, &r);
  if (ERROR_SUCCESS == err) {
    return r;
  }
  return (-1) * error_to_errno(err);
}
