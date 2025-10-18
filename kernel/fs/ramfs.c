#include <assert.h>
#include <error.h>
#include <fs/ramfs.h>
#include <kmalloc.h>
#include <kprintf.h>

struct ramfs_file {
  struct sv filename;
  bool (*open)(struct vfs_fd *fd, struct sv file, int flags,
               void *internal_object, int *err);
  void *internal_object;
  struct ramfs_file *next;
};

struct vfs_fd *ramfs_open(struct vfs_mount *mount, struct sv file, int flags,
                          int *err) {
  struct ramfs_file *p = (struct ramfs_file *)mount->internal_object;
  for (; p; p = p->next) {
    if (sv_eq(file, p->filename)) {
      break;
    }
  }

  if (!p) {
    return NULL;
  }

  struct vfs_fd *fd = kcalloc(1, sizeof(struct vfs_fd));
  fd->outside_references = 0;
  fd->close = NULL;
  fd->offset = 0;
  fd->read = NULL;
  fd->write = NULL;
  fd->lseek = NULL;
  fd->mmap = NULL;

  assert(p->open);
  assert(true == p->open(fd, file, flags, p->internal_object, err));

  return fd;
}

bool ramfs_add_file(struct vfs_mount *mount, struct sv file,
                    bool (*open)(struct vfs_fd *fd, struct sv file, int flags,
                                 void *internal_object, int *err),
                    void *internal_object, err_t *err) {
  struct ramfs_file *new_file = kmalloc(sizeof(struct ramfs_file));
  if (!new_file) {
    ASSIGN_PTR(err, ERROR_NO_MEMORY);
    return false;
  }

  (void)sv_take(file, &file, sv_length(mount->path));
  new_file->filename = sv_clone(file);
  new_file->open = open;
  new_file->internal_object = internal_object;

  struct ramfs_file *p = (struct ramfs_file *)mount->internal_object;
  new_file->next = p;
  mount->internal_object = new_file;
  return true;
}

struct vfs_mount *ramfs_create(void) {
  struct vfs_mount *mount = kmalloc(sizeof(struct vfs_mount));
  if (!mount) {
    return NULL;
  }
  mount->open = ramfs_open;
  mount->internal_object = NULL; // Linked list of files
                                 // FIXME: That is not very efficent
  return mount;
}
