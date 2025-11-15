#include <unistd.h>

#include <syscall.h>
#include <syscalls.h>

err_t sa_pipe(u64 fd[2]) {
  return syscall(SYS_PIPE, (u64)fd, 0, 0, 0, 0);
}
