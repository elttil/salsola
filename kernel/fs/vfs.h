#ifndef VFS_H
#define VFS_H
#include <stdbool.h>
#include <sv.h>

struct vfs_mount {
  struct sv path;
  struct vfs_fd *root_fd;
  struct vfs_fd *(*open)(struct vfs_mount *mount, struct sv file, int flags,
                         int *err);
  struct vfs_mount *next;
};

struct vfs_fd {};

bool vfs_init(void);
struct vfs_fd *vfs_open(struct sv file, int flags, int *err);
bool vfs_add_mount(struct sv path, struct vfs_mount *root);

#endif // VFS_H
