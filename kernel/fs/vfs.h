#ifndef VFS_H
#define VFS_H
#include <error.h>
#include <stdbool.h>
#include <sv.h>
#include <sys/types.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

enum {
  VFS_TYPE_FILE = 0,
  VFS_TYPE_BLOCK_DEVICE,
  VFS_TYPE_CHAR_DEVICE,
};

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
  size_t (*write)(struct vfs_fd *fd, const void *buffer, size_t length,
                  size_t offset, err_t *err);
  err_t (*lseek)(struct vfs_fd *fd, off_t offset, int whence, off_t *out);

  int type;
  void *internal_object;
  void (*close)(struct vfs_fd *fd);

  // Is set by the VFS, not the FS
  size_t offset;
  struct vfs_mount *mount;
};

bool vfs_init(void);
struct vfs_fd *vfs_open(struct sv file, int flags, err_t *err);
bool vfs_add_mount(struct sv path, struct vfs_mount *root);
struct vfs_mount *vfs_find_mount(struct sv path);
size_t vfs_pread(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                 err_t *err);
size_t vfs_read(struct vfs_fd *fd, void *buffer, size_t length, err_t *err);
size_t vfs_write(struct vfs_fd *fd, const void *buffer, size_t length,
                 err_t *err);
err_t vfs_lseek(struct vfs_fd *fd, off_t offset, int whence, off_t *out);
void vfs_close(struct vfs_fd *fd);

#endif // VFS_H
