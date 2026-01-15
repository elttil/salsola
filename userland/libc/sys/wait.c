#include <errno.h>
#include <error.h>
#include <stdint.h>
#include <sys/wait.h>

pid_t wait(int *stat_loc) {
  pid_t pid;
  uint8_t rc;
  err_t err = waitfd(-1, &rc, &pid);
  if (ERROR_SUCCESS != err) {
    errno = error_to_errno(err);
    return -1;
  }
  if (stat_loc) {
    *stat_loc = rc;
  }
  return pid;
}
