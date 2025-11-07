#include <error.h>
#include <stddef.h>
#include <typedefs.h>

#define MAP_FAILED ((void *)-1)

#define PROT_READ (1 << 0)
#define PROT_WRITE (1 << 1)
#define PROT_EXEC (1 << 2)

#define MAP_PRIVATE (1 << 0)
#define MAP_ANONYMOUS (1 << 1)
#define MAP_SHARED (1 << 2)
#define MAP_STACK (1 << 3)

#ifndef KERNEL
err_t sa_mmap(void *addr, size_t length, int prot, int flags, u64 fd,
              size_t offset, void **out);
void *mmap(void *addr, size_t length, int prot, int flags, int fd,
           size_t offset);
int munmap(void *addr, size_t length);
#endif // KERNEL
