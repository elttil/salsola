#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

#include <stdio.h>

err_t sa_fork(pid_t *pid) {
  return syscall(SYS_FORK, (u64)pid, 0, 0, 0, 0);
}

pid_t fork(void) {
  pid_t pid;
  err_t err = sa_fork(&pid);
  if (ERROR_SUCCESS != err) {
    // TODO: Handle fork() error
    printf("TODO: Handle fork() error\n");
    return -1;
  }
  return pid;
}
