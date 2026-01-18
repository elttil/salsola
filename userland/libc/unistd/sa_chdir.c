#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

err_t sa_chdir(struct sv path) {
  return syscall(SYS_CHDIR, (u64)sv_buffer(path), (u64)sv_length(path), 0, 0,
                 0);
}
