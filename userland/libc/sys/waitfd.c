#include <sys/wait.h>
#include <error.h>
#include <syscall.h>
#include <syscalls.h>

err_t waitfd(int fd, uint8_t *error_code) {
	return syscall(SYS_WAITFD, (u64)fd, (u64)error_code, 0, 0, 0);
}
