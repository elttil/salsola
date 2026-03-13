#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

err_t sa_pwrite(int fd, const void *buf, u64 count, u64 offset, u64 *out) {
  return syscall(SYS_PWRITE, fd, (u64)buf, count, offset, (u64)out);
}
