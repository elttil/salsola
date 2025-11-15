#include <assert.h>
#include <error.h>
#include <fs/pipe.h>
#include <fs/vfs.h>
#include <kmalloc.h>
#include <lock.h>
#include <ringbuffer.h>

struct pipe {
  struct ringbuffer buffers[2];
  struct vfs_fd *fds[2];
  int references;
  lock_t lock;
};

#define PIPE_TYPE_FIRST 1
#define PIPE_TYPE_SECOND 2

void pipe_free(struct pipe *p, bool skip) {
  lock_acquire(&p->lock);
  assert(0 != p->references && !skip);
  p->references--; // TODO: Maybe do an atomic decrement instead
  if (0 != p->references && !skip) {
    lock_release(&p->lock);
    return;
  }

  ringbuffer_free(&p->buffers[0]);
  ringbuffer_free(&p->buffers[1]);
  kfree(p);
}

static void send_update(struct ringbuffer *rb, struct vfs_fd *writer,
                        struct vfs_fd *reader) {
  bool has_data = (0 != ringbuffer_used(rb));
  vfs_notify_can_read(reader, has_data);
  bool can_write = (0 != ringbuffer_unused(rb));
  vfs_notify_can_write(writer, can_write);
}

size_t pipe_write(struct vfs_fd *fd, const void *buffer, size_t length,
                  size_t offset, err_t *err) {
  (void)offset;
  ASSIGN_PTR(err, ERROR_SUCCESS);
  struct pipe *p = fd->internal_object;

  lock_acquire(&p->lock);

  struct ringbuffer *rb;
  struct vfs_fd *other;
  if (PIPE_TYPE_FIRST == fd->internal_object_type) {
    rb = &p->buffers[1];
    other = p->fds[1];
  } else {
    rb = &p->buffers[0];
    other = p->fds[0];
  }
  size_t rc = ringbuffer_write(rb, buffer, length);
  send_update(rb, fd, other);

  lock_release(&p->lock);

  return rc;
}

size_t pipe_read(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                 err_t *err) {
  (void)offset;
  ASSIGN_PTR(err, ERROR_SUCCESS);

  struct pipe *p = fd->internal_object;

  lock_acquire(&p->lock);

  struct ringbuffer *rb;
  struct vfs_fd *other;
  if (PIPE_TYPE_FIRST == fd->internal_object_type) {
    rb = &p->buffers[0];
    other = p->fds[0];
  } else {
    rb = &p->buffers[1];
    other = p->fds[1];
  }
  size_t rc = ringbuffer_read(rb, buffer, length);
  send_update(rb, other, fd);

  lock_release(&p->lock);

  return rc;
}

err_t pipe(struct vfs_fd *fd[2]) {
  struct pipe *p = kmalloc(sizeof(struct pipe));
  if (!p) {
    return ERROR_NO_MEMORY;
  }
  if (!ringbuffer_init(&p->buffers[0], 8192)) {
    kfree(p);
    return ERROR_NO_MEMORY;
  }
  if (!ringbuffer_init(&p->buffers[1], 8192)) {
    ringbuffer_free(&p->buffers[0]);
    kfree(p);
    return ERROR_NO_MEMORY;
  }
  p->references = 0;
  lock_release(&p->lock);

  for (int i = 0; i < 2; i++) {
    fd[i] = vfs_allocate_fd();
    if (!fd[i]) {
      kfree(fd[i]);
      if (1 == i) {
        kfree(fd[0]);
      }
      pipe_free(p, true);
      return ERROR_NO_MEMORY;
    }
    fd[i]->read = pipe_read;
    fd[i]->write = pipe_write;
    fd[i]->internal_object_type = (0 == i) ? PIPE_TYPE_FIRST : PIPE_TYPE_SECOND;
    fd[i]->internal_object = p;
    p->references++;
  }
  p->fds[0] = fd[0];
  p->fds[1] = fd[1];
  return ERROR_SUCCESS;
}
