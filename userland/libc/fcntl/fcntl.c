#include <fcntl.h>
#include <stdarg.h>
#include <syscall.h>

#include <todo.h>

int fcntl(int fd, int cmd, ...) {
  (void)fd;
  (void)cmd;
  TODO("fcntl");
  return 0;
  /*
  int arg = 0;

  if ((F_GETFL == cmd) || (F_SETFL == cmd)) {
    va_list ap;
    va_start(ap, cmd);
    arg = va_arg(ap, int);
    va_end(ap);
  }
  RC_ERRNO(syscall(SYS_FCNTL, fd, cmd, arg, 0, 0));
  */
}
