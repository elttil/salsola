#include <assert.h>
#include <fs/vfs.h>
#include <kmalloc.h>
#include <stdbool.h>

struct mount_list {
  struct vfs_mount *mount;
  struct mount_list *next;
};

struct mount_list *mount_head = NULL;

struct vfs_mount *vfs_find_mount(struct sv path) {
  struct mount_list *p = mount_head;
  for (; p; p = p->next) {
    if (sv_partial_eq(path, p->mount->path)) {
      return p->mount;
    }
  }
  return NULL;
}

bool vfs_add_mount(struct sv path, struct vfs_mount *root) {
  assert(!vfs_find_mount(path));

  struct mount_list *mount = kmalloc(sizeof(struct mount_list));
  if (!mount) {
    return false;
  }

  mount->mount = root;
  root->path = sv_clone(path);

  mount->next = mount_head;
  mount_head = mount;

  return true;
}

struct vfs_fd *vfs_open(struct sv file, int flags, int *err) {
  struct vfs_mount *mount = vfs_find_mount(file);
  assert(mount); // TODO

  (void)sv_take(file, &file, sv_length(mount->path));
  return mount->open(mount, file, flags, err);
}
