#include <error.h>
#include <stdint.h>

int wait(int *stat_loc);
err_t waitfd(int fd, uint8_t *error_code);
