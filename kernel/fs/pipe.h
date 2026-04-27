#include <error.h>
#include <fs/vfs.h>

err_t pipe(struct vfs_fd *fd[2], size_t ringbuffer_size);
