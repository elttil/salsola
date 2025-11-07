#include <assert.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <sys/stat.h>
#include <syscall.h>
#include <syscalls.h>

err_t sa_fstat(u64 fd, struct stat *buf) {
  return syscall(SYS_FSTAT, fd, (u64)buf, 0, 0, 0);
}

int fstat(int fd, struct stat *buf) {
  return error_to_errno(sa_fstat(fd, buf));
}
