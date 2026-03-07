#include <unistd.h>

#include <syscall.h>
#include <syscalls.h>

void msleep(u64 ms) {
  syscall(SYS_MSLEEP, ms, 0, 0, 0, 0);
  return;
}
