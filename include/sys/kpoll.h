#ifndef KPOLL_H
#define KPOLL_H
#include <error.h>
#include <stddef.h>
#include <typedefs.h>

#define KEVENT_CAN_READ (1 << 0)
#define KEVENT_CAN_WRITE (1 << 1)

#define KEVENT_NOP_FD 0
#define KEVENT_ADD_FD 1
#define KEVENT_MOD_FD 2
#define KEVENT_KERNEL_RETURN 3

struct kevent {
  u64 fd;
  u8 flags;
  u8 mod;
};

err_t kpoll(u64 fd, struct kevent *events, size_t nevents, size_t *nchanges);

#ifdef KERNEL
#include <fs/vfs.h>
#include <list.h>
#include <lock.h>
struct listener {
  lock_t lock;
  u64 num_fd;

  // NOTE: These pointers shall be NULL before they are freed. As a
  // result a simple lock on this structure ensures that the pointers
  // remain accesible while the lock is acquired by the VFS.
  struct kpoll *poll;
  struct vfs_fd *fd;

  u8 flags;

  bool has_sent_update;
};

DEFINE_LIST_STRUCT(list_listener, struct listener *)

struct kpoll {
  lock_t lock;
  // TODO: Make this into a better data structure to find the file
  // descriptor the caller is looking for.(Currently it is O(n))
  struct list_listener_ctx list;
  struct list_listener_ctx updates;
};

bool kpoll_add_device(void);
#endif // KERNEL
#endif // KPOLL_H
