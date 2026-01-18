#include <assert.h>
#include <fs/ramfs.h>
#include <sys/kpoll.h>
#include <task.h>

#include <kprintf.h>

#define KPOLL_MAGIC 0x9d539cee

DEFINE_LIST_FUNCTIONS(list_listener, struct listener *);

err_t kpoll(u64 fd, struct kevent *events, size_t nevents, size_t *nchanges) {
  struct vfs_fd *k_fd;

  GET_FD(fd, &k_fd);

  if (KPOLL_MAGIC != k_fd->internal_object_type) {
    return ERROR_FD_HAS_NO_KPOLL;
  }

  struct kpoll *kpoll = k_fd->internal_object;

  lock_acquire(&kpoll->lock);

  for (size_t i = 0; i < nevents; i++) {
    struct kevent *ev = &events[i];
    if (KEVENT_NOP_FD == ev->mod) {
      continue;
    }
    if (KEVENT_KERNEL_RETURN == ev->mod) {
      continue;
    }
    if (KEVENT_MOD_FD == ev->mod) {
      for (size_t j = 0;; j++) {
        struct listener *listener;
        if (!list_listener_get(&kpoll->list, j, &listener)) {
          break;
        }
        if (!listener || listener->num_fd != ev->fd) {
          continue;
        }
        if (listener->flags != ev->flags) {
          listener->has_sent_update = false;
        }
        listener->flags = ev->flags;
        continue;
      }
      assert(0); // TODO: Return a error maybe?
    }
    if (KEVENT_ADD_FD == ev->mod) {
      struct vfs_fd *fd_ptr;
      if (!list_fd_get(&get_current_task()->fds, ev->fd, &fd_ptr) || !fd_ptr) {
        // TODO: Handle and give an error.
        continue;
      }

      struct listener *listener = kmalloc(sizeof(struct listener));
      if (!listener) {
        assert(0); // TODO: How to handle this?
      }

      lock_release(&listener->lock);

      listener->poll = kpoll;
      listener->fd = fd_ptr;
      listener->num_fd = ev->fd;
      listener->flags = ev->flags;
      listener->has_sent_update = false;

      if (!list_listener_add(&kpoll->list, listener, NULL)) {
        // TODO: Handle error
        continue;
      }

      lock_release(&kpoll->lock);
      if (ERROR_SUCCESS != vfs_add_listener(fd_ptr, listener)) {
        // TODO: Handle error
        continue;
      }
      lock_acquire(&kpoll->lock);
    }
  }

  memset(events, 0, sizeof(struct kevent) * nevents);

  get_current_task()->active_kpoll = kpoll;
  while (0 == list_listener_num_entries(&kpoll->updates)) {
    lock_release(&kpoll->lock);
    task_legacy_switch();
    lock_acquire(&kpoll->lock);
  }
  get_current_task()->active_kpoll = NULL;

  size_t ch = 0;
  for (; ch < nevents;) {
    struct listener *listener = NULL;
    size_t j = 0;
    events[ch].flags = 0;
    for (;; j++) {
      if (!list_listener_get(&kpoll->updates, j, &listener)) {
        listener = NULL;
        break;
      }
      if (!listener) {
        continue;
      }
      lock_acquire(&listener->lock);
      if ((listener->flags & KEVENT_CAN_READ) && listener->fd->data.can_read) {
        events[ch].flags |= KEVENT_CAN_READ;
      }
      if ((listener->flags & KEVENT_CAN_WRITE) &&
          listener->fd->data.can_write) {
        events[ch].flags |= KEVENT_CAN_WRITE;
      }
      if (0 != events[ch].flags) {
        break;
      }
      listener->has_sent_update = false;
      list_listener_remove(&kpoll->updates, j);
      lock_release(&listener->lock);
      listener = NULL;
    }
    if (!listener) {
      break;
    }

    events[ch].mod = KEVENT_KERNEL_RETURN;
    events[ch].fd = listener->num_fd;

    if (0 != events[ch].flags) {
      listener->has_sent_update = false;
      ch++;
    }
    lock_release(&listener->lock);
  }

  lock_release(&kpoll->lock);

  if (nchanges) {
    *nchanges = ch;
  }

  return ERROR_SUCCESS;
}

bool kpoll_open(struct vfs_fd *fd, struct sv file, int flags,
                void *internal_object, int *err) {
  (void)file;
  (void)flags;
  (void)internal_object;
  ASSIGN_PTR(err, ERROR_SUCCESS);
  fd->type = VFS_TYPE_CHAR_DEVICE;

  struct kpoll *kpoll = kmalloc(sizeof(struct kpoll));
  if (!kpoll) {
    ASSIGN_PTR(err, ERROR_NO_MEMORY);
    return false;
  }
  lock_release(&kpoll->lock);
  fd->internal_object_type = KPOLL_MAGIC;
  fd->internal_object = kpoll;

  list_listener_init(&kpoll->list);
  list_listener_init(&kpoll->updates);
  return true;
}

bool kpoll_add_device(void) {
  struct vfs_mount *mount = vfs_find_mount(C_TO_SV("/dev"));
  if (!mount) {
    return false;
  }
  struct sv filename = C_TO_SV("/dev/kpoll");
  return ramfs_add_file(mount, filename, kpoll_open, NULL, NULL);
}
