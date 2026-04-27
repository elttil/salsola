#ifndef VFS_H
#define VFS_H
#include <dirent.h>
#include <error.h>
#include <stdbool.h>
#include <sv.h>
#include <sys/kpoll.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define VFS_UNIQUE_TYPE_PROCESS 0xc8c4917e

enum {
  VFS_TYPE_FILE = 0,
  VFS_TYPE_BLOCK_DEVICE,
  VFS_TYPE_CHAR_DEVICE,
  VFS_TYPE_DIRECTORY,
};

struct vfs_mount {
  struct sv path;
  struct vfs_fd *(*open)(struct vfs_mount *mount, struct sv file, int flags,
                         err_t *err);
  rwlock_t rwlock;
  void *internal_object;
  struct vfs_mount *next;
};

struct fd_data {
  bool can_read;
  bool can_write;
  bool is_closed;
};

struct vfs_fd {
  struct vfs_fd *(*open)(struct vfs_mount *mount, struct sv file, int flags,
                         err_t *err);
  err_t (*read)(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                size_t *rc);
  err_t (*write)(struct vfs_fd *fd, const void *buffer, size_t length,
                 size_t offset, size_t *rc);
  err_t (*lseek)(struct vfs_fd *fd, off_t offset, int whence, off_t *out);
  err_t (*mmap)(struct vfs_fd *fd, void *addr, size_t length, int prot,
                int flags, size_t offset, void **out);
  err_t (*getdent)(struct vfs_fd *fd, struct vfs_dirent **dirp,
                   size_t *dirp_size, size_t offset);
  err_t (*truncate)(struct vfs_fd *fd, u64 length);
  err_t (*sendfd)(struct vfs_fd *fd, struct vfs_fd *inc);
  err_t (*recvfd)(struct vfs_fd *fd, struct vfs_fd **out);
  err_t (*stat)(struct vfs_fd *fd, struct stat *buf);

  // TODO: Add a lock
  int type;
  int flags;
  u32 internal_object_type;
  void *internal_object;
  void (*close)(struct vfs_fd *fd);

  bool is_blocking;

  u32 references;
  u32 outside_references;

  lock_t listeners_lock;
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
WARN_UNUSED err_t vfs_lseek(struct vfs_fd *fd, off_t offset, int whence,
                            off_t *out);
WARN_UNUSED err_t vfs_fstat(struct vfs_fd *fd, struct stat *buf);
WARN_UNUSED err_t vfs_mmap(struct vfs_fd *fd, void *addr, size_t length,
                           int prot, int flags, size_t offset, void **out);
WARN_UNUSED err_t vfs_pread(struct vfs_fd *fd, void *buffer, size_t length,
                            size_t offset, size_t *rc);
WARN_UNUSED err_t vfs_read(struct vfs_fd *fd, void *buffer, size_t length,
                           size_t *rc);
WARN_UNUSED err_t vfs_pwrite(struct vfs_fd *fd, const void *buffer,
                             size_t length, size_t offset, size_t *rc);
WARN_UNUSED err_t vfs_write(struct vfs_fd *fd, const void *buffer,
                            size_t length, size_t *rc);
WARN_UNUSED err_t vfs_truncate(struct vfs_fd *fd, u64 length);
void vfs_close(struct vfs_fd *fd);
void vfs_notify_listeners(struct vfs_fd *fd);
void vfs_notify_can_read(struct vfs_fd *fd, bool can_read);
void vfs_notify_can_write(struct vfs_fd *fd, bool can_write);
WARN_UNUSED err_t vfs_add_listener(struct vfs_fd *fd,
                                   struct listener *listener);
WARN_UNUSED err_t vfs_getdent(struct vfs_fd *fd, struct vfs_dirent *dirp,
                              size_t dir_entry_size, u64 nentries, u64 *rc);
WARN_UNUSED err_t vfs_sendfd(struct vfs_fd *fd, struct vfs_fd *inc);
WARN_UNUSED err_t vfs_recvfd(struct vfs_fd *fd, struct vfs_fd **out);

#endif // VFS_H
