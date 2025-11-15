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

static void vfs_notify_listeners(struct vfs_fd *fd) {
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

    lock_acquire(&poll->lock);
    list_listener_add_or_replace_previous_null(&poll->updates, listener, NULL);
    lock_release(&poll->lock);

    listener->has_sent_update = true;

    lock_release(&listener->lock);
  }
}

err_t vfs_add_listener(struct vfs_fd *fd, struct listener *listener) {
  TRY(list_listener_add(&fd->listeners, listener, NULL));

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

size_t vfs_pread(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                 err_t *err) {
  ASSIGN_PTR(err, ERROR_SUCCESS);
  if (!fd) {
    ASSIGN_PTR(err, ERROR_INVALID_FD);
    return 0;
  }
  if (!fd->read) {
    ASSIGN_PTR(err, ERROR_FD_HAS_NO_READ);
    return 0;
  }
  return fd->read(fd, buffer, length, offset, err);
}

size_t vfs_read(struct vfs_fd *fd, void *buffer, size_t length, err_t *err) {
  ASSIGN_PTR(err, ERROR_SUCCESS);
  size_t r = vfs_pread(fd, buffer, length, fd->offset, err);
  if (VFS_TYPE_CHAR_DEVICE != fd->type) {
    fd->offset += r;
  }
  return r;
}

size_t vfs_pwrite(struct vfs_fd *fd, const void *buffer, size_t length,
                  size_t offset, err_t *err) {
  ASSIGN_PTR(err, ERROR_SUCCESS);
  if (!fd) {
    ASSIGN_PTR(err, ERROR_INVALID_FD);
    return 0;
  }
  if (!fd->write) {
    ASSIGN_PTR(err, ERROR_FD_HAS_NO_WRITE);
    return 0;
  }
  return fd->write(fd, buffer, length, offset, err);
}

size_t vfs_write(struct vfs_fd *fd, const void *buffer, size_t length,
                 err_t *err) {
  ASSIGN_PTR(err, ERROR_SUCCESS);
  size_t r = vfs_pwrite(fd, buffer, length, fd->offset, err);
  if (VFS_TYPE_CHAR_DEVICE != fd->type) {
    fd->offset += r;
  }
  return r;
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
  // Check if there are existing mmaps that rely upon the vfs_fd object
  // existing
  if (0 != fd->outside_references) {
    return;
  }

  if (fd->close) {
    fd->close(fd);
  }
  kfree(fd);
}

struct vfs_fd *vfs_open(struct sv file, int flags, err_t *err) {
  ASSIGN_PTR(err, ERROR_SUCCESS);
  struct vfs_mount *mount = vfs_find_mount(file);
  assert(mount); // TODO
  assert(mount->open);

  (void)sv_take(file, &file, sv_length(mount->path));
  struct vfs_fd *fd = mount->open(mount, file, flags, err);
  if (!fd) {
    return NULL;
  }
  fd->mount = mount;
  return fd;
}
