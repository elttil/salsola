#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <syscall.h>
#include <syscalls.h>
#include <tb/sv.h>

err_t sa_open(int *fd, struct sv path, int flags, mode_t mode) {
  return syscall(SYS_OPEN, (u64)fd, (u64)sv_buffer(path), (u64)sv_length(path),
                 flags, mode);
}

int open(const char *file, int flags, ...) {
  mode_t mode = 0;

  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }

  struct sv path = C_TO_SV(file);
  int fd;
  err_t err = sa_open(&fd, path, flags, mode);
  if (ERROR_SUCCESS != err) {
    errno = error_to_errno(err);
    return -1;
  }
  return fd;
}
