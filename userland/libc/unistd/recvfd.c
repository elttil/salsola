#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

err_t sa_recvfd(u64 fd, u64 *out_fd) {
  return syscall(SYS_RECVFD, fd, (u64)out_fd, 0, 0, 0);
}

int recvfd(int fd) {
  u64 out;
  err_t error = sa_recvfd(fd, &out);
  if (ERROR_SUCCESS != error) {
    errno = error_to_errno(error);
    return -1;
  }
  return out;
}
