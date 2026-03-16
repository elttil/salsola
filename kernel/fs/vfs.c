#include <assert.h>
#include <fcntl.h>
#include <fs/vfs.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <stdbool.h>
#include <task.h>

struct mount_list {
  struct vfs_mount *mount;
  struct mount_list *next;
};

struct mount_list *mount_head = NULL;

struct vfs_mount *vfs_find_mount(struct sv path) {
  struct vfs_mount *longest = NULL;
  size_t max_length = 0;

  struct mount_list *p = mount_head;
  for (; p; p = p->next) {
    if (!sv_partial_eq(path, p->mount->path)) {
      continue;
    }
    if (sv_length(p->mount->path) > max_length) {
      max_length = sv_length(p->mount->path);
      longest = p->mount;
    }
  }
  return longest;
}

struct vfs_fd *vfs_allocate_fd(void) {
  struct vfs_fd *fd = kcalloc(1, sizeof(struct vfs_fd));
  list_listener_init(&fd->listeners);
  fd->is_blocking = true;
  fd->references = 1;
  lock_release(&fd->listeners_lock);
  return fd;
}

static void vfs_notify_listeners(struct vfs_fd *fd) {
  lock_acquire(&fd->listeners_lock);
  for (u64 i = 0;; i++) {
    struct listener *listener;
    if (!list_listener_get(&fd->listeners, i, &listener)) {
      break;
    }
    if (!listener) {
      continue;
    }
    if (listener->has_sent_update) {
      continue;
    }
    bool update_read =
        ((KEVENT_CAN_READ & listener->flags) && fd->data.can_read);
    bool update_write =
        ((KEVENT_CAN_WRITE & listener->flags) && fd->data.can_write);

    if (!(update_read || update_write)) {
      continue;
    }

    lock_acquire(&listener->lock);

    struct kpoll *poll = listener->poll;
    if (!poll) {
      // TODO: Deallocate the listener at this point.
      lock_release(&listener->lock);
      continue;
    }

    list_listener_add_or_replace_previous_null(&poll->updates, listener, NULL);

    listener->has_sent_update = true;

    lock_release(&listener->lock);
  }
  lock_release(&fd->listeners_lock);
}

err_t vfs_add_listener(struct vfs_fd *fd, struct listener *listener) {
  lock_acquire(&fd->listeners_lock);
  err_t err = list_listener_add(&fd->listeners, listener, NULL);
  lock_release(&fd->listeners_lock);
  if (ERROR_SUCCESS != err) {
    return err;
  }

  if (VFS_TYPE_BLOCK_DEVICE == fd->type) {
    // The caller isn't(shouldn't be) listening to a block device for figuring
    // out if it can read/write, but if they are then just always tell
    // them they can read/write.
    fd->data.can_read = true;
    fd->data.can_write = true;
  }

  vfs_notify_listeners(fd);

  return ERROR_SUCCESS;
}

void vfs_notify_can_read(struct vfs_fd *fd, bool can_read) {
  fd->data.can_read = can_read;
  if (!can_read) {
    return;
  }
  vfs_notify_listeners(fd);
}

void vfs_notify_can_write(struct vfs_fd *fd, bool can_write) {
  fd->data.can_write = can_write;
  if (!can_write) {
    return;
  }
  vfs_notify_listeners(fd);
}

err_t vfs_truncate(struct vfs_fd *fd, u64 length) {
  if (!fd) {
    return ERROR_INVALID_FD;
  }
  if (!fd->truncate) {
    return ERROR_FD_HAS_NO_TRUNCATE;
  }
  return fd->truncate(fd, length);
}

err_t vfs_getdent(struct vfs_fd *fd, struct vfs_dirent *dirp,
                  size_t dir_entry_size, u64 nentries, u64 *rc) {

  (void)nentries;

  if (!fd) {
    return ERROR_INVALID_FD;
  }
  if (!fd->getdent) {
    return ERROR_FD_HAS_NO_GETDENT;
  }
  struct vfs_dirent *d = NULL;
  size_t s = 0;
  err_t err = fd->getdent(fd, &d, &s, fd->offset);
  if (ERROR_SUCCESS != err) {
    return err;
  }

  size_t entry_length = d->d_namelength + sizeof(struct vfs_dirent) + 1;
  if (entry_length > dir_entry_size) {
    return ERROR_BUFFER_TOO_SMALL;
  }

  fd->offset++;

  memcpy(dirp, d, entry_length);
  ASSIGN_PTR(rc, 1);
  return ERROR_SUCCESS;
}

err_t vfs_pread(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                size_t *rc) {
  if (!fd) {
    return ERROR_INVALID_FD;
  }
  if (!fd->read) {
    return ERROR_FD_HAS_NO_READ;
  }
  if (!fd->is_blocking) {
    return fd->read(fd, buffer, length, offset, rc);
  }
  err_t err = fd->read(fd, buffer, length, offset, rc);
  for (; ERROR_READ_WOULD_BLOCK == err;) {
    task_set_wait(fd, TASK_WAIT_READ);
    err = fd->read(fd, buffer, length, offset, rc);
  }

  return err;
}

err_t vfs_recvfd(struct vfs_fd *fd, struct vfs_fd **out) {
  if (!fd) {
    return ERROR_INVALID_FD;
  }
  if (!fd->recvfd) {
    return ERROR_FD_HAS_NO_RECVFD;
  }
  return fd->recvfd(fd, out);
}

err_t vfs_read(struct vfs_fd *fd, void *buffer, size_t length, size_t *rc) {
  size_t p;
  err_t err = vfs_pread(fd, buffer, length, fd->offset, &p);
  if (VFS_TYPE_CHAR_DEVICE != fd->type) {
    fd->offset += p;
  }
  ASSIGN_PTR(rc, p);
  return err;
}

err_t vfs_pwrite(struct vfs_fd *fd, const void *buffer, size_t length,
                 size_t offset, size_t *rc) {
  if (!fd) {
    return ERROR_INVALID_FD;
  }
  if (!fd->write) {
    return ERROR_FD_HAS_NO_WRITE;
  }
  return fd->write(fd, buffer, length, offset, rc);
}

err_t vfs_write(struct vfs_fd *fd, const void *buffer, size_t length,
                size_t *rc) {
  size_t p;
  err_t err = vfs_pwrite(fd, buffer, length, fd->offset, &p);
  if (VFS_TYPE_CHAR_DEVICE != fd->type) {
    fd->offset += p;
  }
  ASSIGN_PTR(rc, p);
  return err;
}

err_t vfs_lseek(struct vfs_fd *fd, off_t offset, int whence, off_t *out) {
  if (!fd) {
    return ERROR_INVALID_FD;
  }
  if (!fd->lseek) {
    return ERROR_FD_HAS_NO_LSEEK;
  }
  return fd->lseek(fd, offset, whence, out);
}

bool vfs_add_mount(struct sv path, struct vfs_mount *root) {
  if (!root) {
    return false;
  }
  //  assert(!vfs_find_mount(path));

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

err_t vfs_mmap(struct vfs_fd *fd, void *addr, size_t length, int prot,
               int flags, size_t offset, void **out) {
  if (!fd) {
    return ERROR_INVALID_FD;
  }
  if (!fd->mmap) {
    return ERROR_FD_HAS_NO_MMAP;
  }
  return fd->mmap(fd, addr, length, prot, flags, offset, out);
}

void vfs_close(struct vfs_fd *fd) {
  assert(0 == fd->outside_references && 0 != fd->references);
  // Check if there are existing mmaps that rely upon the vfs_fd object
  // existing
  if (0 != fd->outside_references) {
    return;
  }

  fd->references--;
  if (0 != fd->references) {
    return;
  }

  if (fd->close) {
    fd->close(fd);
  }
  kfree(fd);
}

struct vfs_fd *vfs_open(struct sv file, int flags, err_t *err) {
  ASSIGN_PTR(err, ERROR_SUCCESS);
  struct vfs_fd *fd = task_find_namespace_override(file);
  if(fd) return fd;

  struct vfs_mount *mount = vfs_find_mount(file);
  assert(mount); // TODO
  assert(mount->open);

  (void)sv_take(file, &file, sv_length(mount->path));
  fd = mount->open(mount, file, flags, err);
  if (!fd) {
    return NULL;
  }
  fd->mount = mount;
  fd->flags = flags;
  if (fd->flags & O_TRUNC) {
    assert(ERROR_SUCCESS == vfs_truncate(fd, 0));
  }
  return fd;
}
