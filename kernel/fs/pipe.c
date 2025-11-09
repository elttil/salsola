#include <error.h>
#include <fs/pipe.h>
#include <fs/vfs.h>
#include <kmalloc.h>
#include <ringbuffer.h>

struct pipe {
  struct ringbuffer buffers[2];
  int references;
};

#define PIPE_TYPE_FIRST 1
#define PIPE_TYPE_SECOND 2

void pipe_free(struct pipe *p) {
  ringbuffer_free(&p->buffers[0]);
  ringbuffer_free(&p->buffers[1]);
  kfree(p);
}

size_t pipe_write(struct vfs_fd *fd, const void *buffer, size_t length,
                  size_t offset, err_t *err) {
  (void)offset;
  ASSIGN_PTR(err, ERROR_SUCCESS);
  struct pipe *p = fd->internal_object;

  struct ringbuffer *rb;
  if (PIPE_TYPE_FIRST == fd->internal_object_type) {
    rb = &p->buffers[1];
  } else {
    rb = &p->buffers[0];
  }
  return ringbuffer_write(rb, buffer, length);
}

size_t pipe_read(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                 err_t *err) {
  (void)offset;
  ASSIGN_PTR(err, ERROR_SUCCESS);

  struct pipe *p = fd->internal_object;

  struct ringbuffer *rb;
  if (PIPE_TYPE_FIRST == fd->internal_object_type) {
    rb = &p->buffers[0];
  } else {
    rb = &p->buffers[1];
  }
  return ringbuffer_read(rb, buffer, length);
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

  for (int i = 0; i < 2; i++) {
    fd[i] = kcalloc(1, sizeof(struct vfs_fd));
    if (!fd[i]) {
      kfree(fd[i]);
      if (1 == i) {
        kfree(fd[0]);
      }
      pipe_free(p);
      return ERROR_NO_MEMORY;
    }
    fd[i]->read = pipe_read;
    fd[i]->write = pipe_write;
    fd[i]->internal_object_type = (0 == i) ? PIPE_TYPE_FIRST : PIPE_TYPE_SECOND;
    fd[i]->internal_object = p;
    p->references++;
  }
  return ERROR_SUCCESS;
}
