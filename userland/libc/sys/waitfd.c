#include <error.h>
#include <sys/wait.h>
#include <syscall.h>
#include <syscalls.h>

err_t waitfd(int fd, uint8_t *error_code, pid_t *pid) {
  return syscall(SYS_WAITFD, (u64)fd, (u64)error_code, (u64)pid, 0, 0);
}
