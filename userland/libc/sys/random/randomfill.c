#include <sys/random.h>
#include <syscall.h>
#include <syscalls.h>

void randomfill(void *buffer, uint32_t size) {
  syscall(SYS_RANDOMFILL, (u64)buffer, size, 0, 0, 0);
}
