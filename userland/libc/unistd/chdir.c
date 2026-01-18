#include <errno.h>
#include <unistd.h>

int chdir(const char *path) {
  err_t err = sa_chdir(C_TO_SV(path));
  if (ERROR_SUCCESS == err) {
    return 0;
  }
  errno = error_to_errno(err);
  return -1;
}
