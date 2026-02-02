#include <errno.h>
#include <sys/mman.h>
#include <syscall.h>
#include <syscalls.h>

#include <stdio.h>

err_t sa_mmap(void *addr, size_t length, int prot, int flags, u64 fd,
              size_t offset, void **out) {
  return syscall_long(SYS_MMAP, (u64)addr, length, prot, flags, fd, offset,
                      (u64)out, 0xDEADBEEF);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd,
           size_t offset) {
  (void)addr;
  (void)length;
  (void)prot;
  (void)flags;
  (void)fd;
  (void)offset;
  printf("TODO: mmap\n");
  for (;;)
    ;
}
