#include <stdlib.h>
#include <syscall.h>
#include <syscalls.h>

void exit(int status) {
  (void)syscall(SYS_EXIT, (u64)status, 0, 0, 0, 0);
}
