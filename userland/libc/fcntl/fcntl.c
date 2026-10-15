#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <syscall.h>
#include <syscalls.h>

int fcntl(int fd, int cmd, ...) {
  int arg = 0;

  if ((F_GETFL == cmd) || (F_SETFL == cmd)) {
    va_list ap;
    va_start(ap, cmd);
    arg = va_arg(ap, int);
    va_end(ap);
  }
  err_t err = syscall(SYS_FCNTL, fd, cmd, arg, 0, 0);
  if (ERROR_SUCCESS != err) {
    errno = error_to_errno(err);
    return -1;
  }
  return 0;
}
