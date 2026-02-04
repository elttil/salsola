#include <unistd.h>

#include <syscalls.h>
#include <syscall.h>

int ftruncate(int fd, off_t length) {
  return error_to_errno(syscall(SYS_FTRUNCATE, fd, length, 0,0,0));
}
