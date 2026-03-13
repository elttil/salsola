#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

err_t sa_pread(int fd, void *buf, u64 count, u64 offset, u64 *out) {
  return syscall(SYS_PREAD, fd, (u64)buf, count, offset, (u64)out);
}
