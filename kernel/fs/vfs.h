#ifndef VFS_H
#define VFS_H
#include <error.h>
#include <stdbool.h>
#include <sv.h>

struct vfs_mount {
  struct sv path;
  struct vfs_fd *(*open)(struct vfs_mount *mount, struct sv file, int flags,
                         err_t *err);
  void *internal_object;
  struct vfs_mount *next;
};

struct vfs_fd {
  size_t (*read)(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                 err_t *err);
  void *internal_object;
  void (*close)(struct vfs_fd *fd);

  // Is set by the VFS, not the FS
  size_t offset;
  struct vfs_mount *mount;
};

bool vfs_init(void);
struct vfs_fd *vfs_open(struct sv file, int flags, int *err);
bool vfs_add_mount(struct sv path, struct vfs_mount *root);
struct vfs_mount *vfs_find_mount(struct sv path);
size_t vfs_pread(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                 err_t *err);
size_t vfs_read(struct vfs_fd *fd, void *buffer, size_t length, err_t *err);
void vfs_close(struct vfs_fd *fd);

#endif // VFS_H
