#include <assert.h>
#include <error.h>
#include <fs/pipe.h>
#include <fs/vfs.h>
#include <kmalloc.h>
#include <lock.h>
#include <ringbuffer.h>

#define PIPE_MAX_FD_TRANSFER 32

struct fd_stack {
  struct vfs_fd *fds[PIPE_MAX_FD_TRANSFER];
  u32 stack_ptr;
};

struct pipe {
  struct ringbuffer buffers[2];
  struct vfs_fd *fds[2];
  struct fd_stack stacks[2];
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

void pipe_close(struct vfs_fd *fd) {
  struct pipe *p = fd->internal_object;
  if (!p) {
    return;
  }
  for (size_t i = 0; i < 2; i++) {
    if (fd == p->fds[i]) {
      p->fds[i] = NULL;
    }
  }
  pipe_free(p, false);
}

static void send_update(struct ringbuffer *rb, size_t stack_ptr,
                        struct vfs_fd *writer, struct vfs_fd *reader) {
  bool has_data = (0 != ringbuffer_used(rb)) || (stack_ptr > 0);
  if (reader) {
    vfs_notify_can_read(reader, has_data);
  }
  bool can_write =
      (0 != ringbuffer_unused(rb)) && (stack_ptr != PIPE_MAX_FD_TRANSFER - 1);
  if (writer) {
    vfs_notify_can_write(writer, can_write);
  }
}

err_t pipe_sendfd(struct vfs_fd *fd, struct vfs_fd *inc) {
  struct pipe *p = fd->internal_object;

  lock_acquire(&p->lock);
  struct ringbuffer *rb;
  struct fd_stack *stack;
  struct vfs_fd *other;
  if (PIPE_TYPE_FIRST == fd->internal_object_type) {
    rb = &p->buffers[1];
    stack = &p->stacks[1];
    other = p->fds[1];
  } else {
    rb = &p->buffers[0];
    stack = &p->stacks[0];
    other = p->fds[0];
  }
  if (PIPE_MAX_FD_TRANSFER - 1 == stack->stack_ptr) {
    lock_release(&p->lock);
    return ERROR_SENDFD_WOULD_BLOCK;
  }

  inc->references++;
  stack->fds[stack->stack_ptr] = inc;
  stack->stack_ptr++;

  if (fd) {
    send_update(rb, stack->stack_ptr, fd, other);
  }

  lock_release(&p->lock);

  return ERROR_SUCCESS;
}

err_t pipe_recvfd(struct vfs_fd *fd, struct vfs_fd **out) {
  assert(out);
  struct pipe *p = fd->internal_object;

  lock_acquire(&p->lock);
  struct ringbuffer *rb;
  struct fd_stack *stack;
  struct vfs_fd *other;
  if (PIPE_TYPE_FIRST == fd->internal_object_type) {
    rb = &p->buffers[0];
    stack = &p->stacks[0];
    other = p->fds[0];
  } else {
    rb = &p->buffers[1];
    stack = &p->stacks[1];
    other = p->fds[1];
  }
  if (0 == stack->stack_ptr) {
    lock_release(&p->lock);
    return ERROR_RECVFD_WOULD_BLOCK;
  }

  stack->stack_ptr--;
  *out = stack->fds[stack->stack_ptr];
  // Sanity check
  assert(*out);
  stack->fds[stack->stack_ptr] = NULL;

  if (fd) {
    send_update(rb, stack->stack_ptr, fd, other);
  }

  lock_release(&p->lock);

  return ERROR_SUCCESS;
}

err_t pipe_write(struct vfs_fd *fd, const void *buffer, size_t length,
                 size_t offset, size_t *rc) {
  (void)offset;
  struct pipe *p = fd->internal_object;

  lock_acquire(&p->lock);

  struct ringbuffer *rb;
  struct fd_stack *stack;
  struct vfs_fd *other;
  if (PIPE_TYPE_FIRST == fd->internal_object_type) {
    rb = &p->buffers[1];
    stack = &p->stacks[1];
    other = p->fds[1];
  } else {
    rb = &p->buffers[0];
    stack = &p->stacks[0];
    other = p->fds[0];
  }
  size_t r = ringbuffer_write(rb, buffer, length);
  send_update(rb, stack->stack_ptr, fd, other);

  lock_release(&p->lock);

  ASSIGN_PTR(rc, r);
  if (0 == r && 0 != length) {
    return ERROR_READ_WOULD_BLOCK;
  }

  return ERROR_SUCCESS;
}

err_t pipe_read(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                size_t *rc) {
  (void)offset;

  struct pipe *p = fd->internal_object;

  lock_acquire(&p->lock);

  struct ringbuffer *rb;
  struct fd_stack *stack;
  struct vfs_fd *other;
  if (PIPE_TYPE_FIRST == fd->internal_object_type) {
    rb = &p->buffers[0];
    stack = &p->stacks[0];
    other = p->fds[0];
  } else {
    rb = &p->buffers[1];
    stack = &p->stacks[1];
    other = p->fds[1];
  }
  size_t r = ringbuffer_read(rb, buffer, length);
  send_update(rb, stack->stack_ptr, fd, other);

  lock_release(&p->lock);

  ASSIGN_PTR(rc, r);
  if (0 == r && 0 != length) {
    return ERROR_READ_WOULD_BLOCK;
  }

  return ERROR_SUCCESS;
}

err_t pipe(struct vfs_fd *fd[2], size_t ringbuffer_size) {
  struct pipe *p = kmalloc(sizeof(struct pipe));
  if (!p) {
    return ERROR_NO_MEMORY;
  }
  p->fds[0] = NULL;
  p->fds[1] = NULL;
  if (!ringbuffer_init(&p->buffers[0], ringbuffer_size)) {
    kfree(p);
    return ERROR_NO_MEMORY;
  }
  if (!ringbuffer_init(&p->buffers[1], ringbuffer_size)) {
    ringbuffer_free(&p->buffers[0]);
    kfree(p);
    return ERROR_NO_MEMORY;
  }
  p->stacks[0].stack_ptr = 0;
  p->stacks[1].stack_ptr = 0;
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
    fd[i]->recvfd = pipe_recvfd;
    fd[i]->sendfd = pipe_sendfd;
    fd[i]->close = pipe_close;
    fd[i]->internal_object_type = (0 == i) ? PIPE_TYPE_FIRST : PIPE_TYPE_SECOND;
    fd[i]->internal_object = p;
    p->references++;
  }
  p->fds[0] = fd[0];
  p->fds[1] = fd[1];
  return ERROR_SUCCESS;
}
