#include <error.h>
#include <stdint.h>
#include <sys/types.h>

pid_t wait(int *stat_loc);
err_t waitfd(int fd, uint8_t *error_code, pid_t *pid);
