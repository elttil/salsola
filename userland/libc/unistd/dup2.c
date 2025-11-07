#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

#include <todo.h>

err_t sa_dup2(u64 oldfd, u64 newfd) {
  return syscall(SYS_DUP2, oldfd, newfd, 0, 0, 0);
}

int dup2(int oldfd, int newfd) {
  err_t err;
if(ERROR_SUCCESS == (err = sa_dup2(oldfd, newfd))) {
    return newfd;
    }
  RC_ERROR_TO_ERRNO(err);
}
