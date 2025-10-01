#include <fs/ramfs.h>
#include <kmalloc.h>
#include <kprintf.h>

struct vfs_fd *ramfs_open(struct vfs_mount *mount, struct sv file, int flags,
                          int *err) {
  (void)mount;
  (void)file;
  (void)flags;
  (void)err;
  kprintf("open: " SV_FMT "\n", SV_FMT_ARG(file));
  struct vfs_fd *fd = kmalloc(sizeof(struct vfs_fd));
  //  fd->open = NULL;
  return fd;
}

struct vfs_mount *ramfs_init(void) {
  struct vfs_mount *mount = kmalloc(sizeof(struct vfs_mount));
  mount->open = ramfs_open;
  return mount;
}
