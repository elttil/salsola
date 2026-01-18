#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

char *getcwd(char *buf, size_t size) {
  err_t err = syscall(SYS_GETCWD, (u64)buf, (u64)size, 0, 0, 0);
  if (ERROR_SUCCESS == err) {
    return buf;
  }
  errno = error_to_errno(err);
  return NULL;
}
