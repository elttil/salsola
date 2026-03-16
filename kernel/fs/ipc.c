#include <assert.h>
#include <fcntl.h>
#include <fs/ipc.h>
#include <fs/pipe.h>
#include <lock.h>
#include <sv.h>

#define IPC_MAX_INCOMING_FD 32

struct ipc_master {
  struct vfs_fd *fd;
  lock_t lock;
  struct vfs_fd *stack[IPC_MAX_INCOMING_FD];
  size_t stack_ptr;
};

struct ipcfs_file {
  struct sv filename;
  struct ipc_master master;
  struct ipcfs_file *next;
};

err_t ipcfs_master_recvfd(struct vfs_fd *fd, struct vfs_fd **out) {
  assert(out);

  struct ipcfs_file *file = (struct ipcfs_file *)fd->internal_object;

  if (0 == file->master.stack_ptr) {
    return ERROR_RECVFD_WOULD_BLOCK;
  }

  lock_acquire(&file->master.lock);

  file->master.stack_ptr--;
  *out = file->master.stack[file->master.stack_ptr];
  assert(*out);
  // Sanity check
  file->master.stack[file->master.stack_ptr] = NULL;
  bool can_read = (0 != file->master.stack_ptr);
  bool can_write = true;

  lock_release(&file->master.lock);

  if (can_read != fd->data.can_read) {
    vfs_notify_can_read(fd, can_read);
  }
  if (can_write != fd->data.can_write) {
    vfs_notify_can_write(fd, can_write);
  }

  return ERROR_SUCCESS;
}

struct vfs_fd *ipcfs_create_master(struct vfs_mount *mount, struct sv file,
                                   int flags, int *err) {
  (void)flags; // TODO
  ASSIGN_PTR(err, ERROR_SUCCESS);

  struct vfs_fd *fd = vfs_allocate_fd();
  if (!fd) {
    ASSIGN_PTR(err, ERROR_NO_MEMORY);
    return NULL;
  }

  struct ipcfs_file *n = kmalloc(sizeof(struct ipcfs_file));
  if (!n) {
    vfs_close(fd);
    ASSIGN_PTR(err, ERROR_NO_MEMORY);
    return NULL;
  }
  if (!sv_clone_err(file, &n->filename)) {
    kfree(n);
    vfs_close(fd);
    ASSIGN_PTR(err, ERROR_NO_MEMORY);
    return NULL;
  }
  n->master.fd = fd;
  n->master.stack_ptr = 0;
  lock_init(&n->master.lock);

  fd->internal_object = (void *)n;
  fd->recvfd = ipcfs_master_recvfd;

  rwlock_write_acquire(&mount->rwlock);
  struct ipcfs_file *next = (struct ipcfs_file *)mount->internal_object;
  n->next = next;
  mount->internal_object = (void *)n;
  rwlock_write_release(&mount->rwlock);
  return fd;
}

struct vfs_fd *ipcfs_open(struct vfs_mount *mount, struct sv file, int flags,
                          int *err) {
  ASSIGN_PTR(err, ERROR_SUCCESS);
  struct ipcfs_file *p = (struct ipcfs_file *)mount->internal_object;
  for (; p; p = p->next) {
    if (sv_eq(file, p->filename)) {
      break;
    }
  }

  if (!p) {
    if (flags & O_CREAT) {
      return ipcfs_create_master(mount, file, flags, err);
    }
    ASSIGN_PTR(err, ERROR_NO_FILE);
    return NULL;
  }
  if (flags & O_CREAT) {
    ASSIGN_PTR(err, ERROR_FILE_ALREADY_EXISTS);
    return NULL;
  }

  lock_acquire(&p->master.lock);
  if (IPC_MAX_INCOMING_FD - 1 == p->master.stack_ptr) {
    ASSIGN_PTR(err, ERROR_MASTER_INCOMING_CONNECTIONS_FULL);
    lock_release(&p->master.lock);
    return NULL;
  }

  // TODO: Maybe move this up? Depends upon what you expect to fail and
  // how messy the code should be/how much you care about the lock being
  // held for a little longer than necessary.
  struct vfs_fd *fds[2];
  err_t r;
  if (ERROR_SUCCESS != (r = pipe(fds, 4096 * 64))) {
    ASSIGN_PTR(err, r);
    lock_release(&p->master.lock);
    return NULL;
  }

  // Since we are returning only fds[0], the fds[1] keeps its reference
  // count (1) and therefore does not need updating.
  p->master.stack[p->master.stack_ptr] = fds[1];
  p->master.stack_ptr++;

  bool can_read = true;

  lock_release(&p->master.lock);

  if (can_read != p->master.fd->data.can_read) {
    vfs_notify_can_read(p->master.fd, can_read);
  }

  return fds[0];
}

struct vfs_mount *ipcfs_create(void) {
  struct vfs_mount *mount = kmalloc(sizeof(struct vfs_mount));
  if (!mount) {
    return NULL;
  }
  rwlock_init(&mount->rwlock);
  mount->open = ipcfs_open;
  mount->internal_object = NULL; // Linked list of files
                                 // FIXME: That is not very efficent
  return mount;
}
