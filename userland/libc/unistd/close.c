#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

#include <todo.h>

err_t sa_close(u64 fd) {
  return syscall(SYS_CLOSE, fd, 0, 0, 0, 0);
}

int close(int fd) {
  RC_ERROR_TO_ERRNO(sa_close(fd));
}
