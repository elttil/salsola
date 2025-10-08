#include <fs/vfs.h>
#include <sv.h>

struct vfs_mount *ramfs_create(void);
bool ramfs_add_file(struct vfs_mount *mount, struct sv file,
                    bool (*open)(struct vfs_fd *fd, struct sv file, int flags,
                                 void *internal_object, int *err),
                    void *internal_object, int *err);
