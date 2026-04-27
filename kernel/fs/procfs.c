#include <assert.h>
#include <error.h>
#include <fs/procfs.h>
#include <kprintf.h>
#include <sb.h>
#include <task.h>

err_t task_pid_read(struct vfs_fd *fd, void *buffer, size_t length,
                    size_t offset, size_t *rc) {
  (void)offset;
  assert(fd->internal_object);

  struct task *task = fd->internal_object;

  struct sb builder;
  sb_init_buffer(&builder, buffer, length);

  ksbprintf(&builder, "%llu", task->pid);

  ASSIGN_PTR(rc, sv_length(SB_TO_SV(builder)));
  return ERROR_SUCCESS;
}

struct vfs_fd *task_open(struct vfs_mount *mount, struct sv path, int flags,
                         int *err) {
  (void)flags;
  ASSIGN_PTR(err, ERROR_SUCCESS);

  struct vfs_fd *fd = vfs_allocate_fd();
  if (!fd) {
    ASSIGN_PTR(err, ERROR_NO_MEMORY);
    return NULL;
  }

  struct task *task = mount->internal_object;
  fd->internal_object = task;
  fd->internal_object_type = VFS_UNIQUE_TYPE_PROCESS;

  if (sv_eq(path, C_TO_SV("pid"))) {
    fd->read = task_pid_read;
    fd->type = VFS_TYPE_BLOCK_DEVICE;
  } else {
    kfree(fd);

    ASSIGN_PTR(err, ERROR_NO_FILE);
    return NULL;
  }

  return fd;
}

err_t open_task_directory(struct task *task, struct vfs_fd **out_fd) {
  struct vfs_fd *fd = vfs_allocate_fd();
  if (!fd) {
    return ERROR_NO_MEMORY;
  }

  // FIXME: Shitty hacky solution
  fd->mount = kcalloc(1, sizeof(struct vfs_mount));
  if (!fd->mount) {
    kfree(fd);
    return ERROR_NO_MEMORY;
  }
  fd->mount->internal_object = task;
  fd->internal_object = task;
  fd->internal_object_type = VFS_UNIQUE_TYPE_PROCESS;
  fd->type = VFS_TYPE_DIRECTORY;

  fd->open = task_open;

  ASSIGN_PTR(out_fd, fd);
  return ERROR_SUCCESS;
}

struct vfs_fd *procfs_open(struct vfs_mount *mount, struct sv file, int flags,
                           int *err) {
  (void)mount;
  (void)flags;
  ASSIGN_PTR(err, ERROR_SUCCESS);

  struct sv dir = sv_split_delim(file, &file, '/');

  struct task *task;
  if (sv_eq(dir, C_TO_SV("self"))) {
    task = get_current_task();
  } else {
    u64 pid;
    int got_num;
    pid = sv_parse_unsigned_number(dir, &dir, &got_num);
    kprintf("pid: %d\n", pid);
    if (!got_num) {
      kprintf("DID NOT GET NUM\n");
      ASSIGN_PTR(err, ERROR_NO_FILE);
      return NULL;
    }
    if (ERROR_SUCCESS != task_get_from_pid(pid, &task)) {
      kprintf("COULD NOT FIND TASK\n");
      ASSIGN_PTR(err, ERROR_NO_FILE);
      return NULL;
    }
  }

  if (0 == sv_length(file)) {
    struct vfs_fd *fd;
    err_t e = open_task_directory(task, &fd);
    ASSIGN_PTR(err, e);
    if (ERROR_SUCCESS != e) {
      return NULL;
    }
    return fd;
  }

  struct vfs_mount m;
  m.internal_object = task;
  return task_open(&m, file, flags, err);
}

struct vfs_mount *procfs_create(void) {
  struct vfs_mount *mount = kmalloc(sizeof(struct vfs_mount));
  if (!mount) {
    return NULL;
  }
  mount->open = procfs_open;
  return mount;
}
