#include <unistd.h>

int pipe(int fd[2]) {
  u64 _fd[2];
  err_t r;
  if (ERROR_SUCCESS != (r = sa_pipe(_fd))) {
    return error_to_errno(r);
  }
  fd[0] = _fd[0];
  fd[1] = _fd[1];
  return 0;
}
