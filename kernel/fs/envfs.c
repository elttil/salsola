#include <assert.h>
#include <fs/envfs.h>
#include <task.h>

void envfs_close(struct vfs_fd *fd) {
  struct environment_variable *env =
      (struct environment_variable *)fd->internal_object;

  lock_acquire(&env->lock);

  assert(0 != env->open_ref_count);
  env->open_ref_count--;

  lock_release(&env->lock);
}

err_t envfs_read(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                 size_t *rc) {
  struct environment_variable *env =
      (struct environment_variable *)fd->internal_object;

  lock_acquire(&env->lock);

  err_t err = sb_read(&env->value, buffer, length, offset, rc);

  lock_release(&env->lock);
  return err;
}

err_t envfs_write(struct vfs_fd *fd, const void *buffer, size_t length,
                  size_t offset, size_t *rc) {
  struct environment_variable *env =
      (struct environment_variable *)fd->internal_object;

  lock_acquire(&env->lock);

  err_t err = sb_write(&env->value, buffer, length, offset, rc);

  lock_release(&env->lock);
  return err;
}

struct vfs_fd *envfs_open(struct vfs_mount *mount, struct sv file, int flags,
                          int *err) {
  (void)mount;
  (void)flags;
  ASSIGN_PTR(err, ERROR_SUCCESS);

  struct vfs_fd *fd = vfs_allocate_fd();
  if (!fd) {
    ASSIGN_PTR(err, ERROR_NO_MEMORY);
    return NULL;
  }
  fd->read = envfs_read;
  fd->write = envfs_write;
  fd->close = envfs_close;

  err_t r = task_variable_add(get_current_task(), file, C_TO_SV(""));
  assert(ERROR_SUCCESS == r || ERROR_VARIABLE_ALREADY_EXISTS == r);

  struct task *task = get_current_task();

  // FIXME: Cleanup the file
  if (ERROR_SUCCESS !=
      task_variable_get(task, file,
                        (struct environment_variable **)&fd->internal_object,
                        true)) {
    // FIXME: Handle this above.
    assert(0);
    ASSIGN_PTR(err, ERROR_NO_FILE);
    return NULL;
  }

  return fd;
}

struct vfs_mount *envfs_create(void) {
  struct vfs_mount *mount = kmalloc(sizeof(struct vfs_mount));
  if (!mount) {
    return NULL;
  }
  mount->open = envfs_open;
  return mount;
}
