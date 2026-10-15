#include <error.h>
#include <sys/kpoll.h>
#include <syscall.h>
#include <syscalls.h>

err_t kpoll(u64 fd, struct kevent *events, size_t nevents, size_t *nchanges,
            i32 timeout) {
  return syscall(SYS_KPOLL, fd, (u64)events, nevents, (u64)nchanges, timeout);
}
