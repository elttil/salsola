#ifndef VFS_H
#define VFS_H
#include <error.h>
#include <stdbool.h>
#include <sv.h>
#include <sys/kpoll.h>
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

struct fd_data {
  bool can_read;
  bool can_write;
};

struct vfs_fd {
  err_t (*read)(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                size_t *rc);
  err_t (*write)(struct vfs_fd *fd, const void *buffer, size_t length,
                 size_t offset, size_t *rc);
  err_t (*lseek)(struct vfs_fd *fd, off_t offset, int whence, off_t *out);
  err_t (*mmap)(struct vfs_fd *fd, void *addr, size_t length, int prot,
                int flags, size_t offset, void **out);

  // TODO: Add a lock
  int type;
  u32 internal_object_type;
  void *internal_object;
  void (*close)(struct vfs_fd *fd);

  bool is_blocking;

  u32 outside_references;

  struct list_listener_ctx listeners;

  struct fd_data data;

  // Is set by the VFS, not the FS
  size_t offset;
  struct vfs_mount *mount;
};

WARN_UNUSED bool vfs_init(void);
WARN_UNUSED struct vfs_fd *vfs_allocate_fd(void);
WARN_UNUSED struct vfs_fd *vfs_open(struct sv file, int flags, err_t *err);
WARN_UNUSED bool vfs_add_mount(struct sv path, struct vfs_mount *root);
WARN_UNUSED struct vfs_mount *vfs_find_mount(struct sv path);
WARN_UNUSED err_t vfs_lseek(struct vfs_fd *fd, off_t offset, int whence, off_t *out);
WARN_UNUSED err_t vfs_mmap(struct vfs_fd *fd, void *addr, size_t length, int prot,
               int flags, size_t offset, void **out);
WARN_UNUSED err_t vfs_pread(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                size_t *rc);
WARN_UNUSED err_t vfs_read(struct vfs_fd *fd, void *buffer, size_t length, size_t *rc);
WARN_UNUSED err_t vfs_pwrite(struct vfs_fd *fd, const void *buffer, size_t length,
                 size_t offset, size_t *rc);
WARN_UNUSED err_t vfs_write(struct vfs_fd *fd, const void *buffer, size_t length,
                size_t *rc);
void vfs_close(struct vfs_fd *fd);
void vfs_notify_can_read(struct vfs_fd *fd, bool can_read);
void vfs_notify_can_write(struct vfs_fd *fd, bool can_write);
WARN_UNUSED err_t vfs_add_listener(struct vfs_fd *fd, struct listener *listener);

#endif // VFS_H
