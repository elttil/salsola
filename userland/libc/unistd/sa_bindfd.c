#include <unistd.h>
#include <tb/sv.h>

#include <syscall.h>
#include <syscalls.h>

err_t sa_bindfd(u64 fd, struct sv path) {
	return syscall(SYS_NAMESPACE_OVERRIDE, (u64)sv_buffer(path), sv_length(path), fd, 0, 0);
}
