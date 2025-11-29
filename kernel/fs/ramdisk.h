#include <fs/vfs.h>
#include <typedefs.h>

struct vfs_fd *ramdisk_create(void *address, u64 size);
