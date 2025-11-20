#include <arch/amd64/idt.h>
#include <arch/amd64/smp.h>
#include <arch/amd64/task_switch.h>
#include <assert.h>
#include <elf.h>
#include <fs/pipe.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <log.h>
#include <stddef.h>
#include <sys/mman.h>
#include <task.h>

DEFINE_LIST_FUNCTIONS(list_fd, struct vfs_fd *)
DEFINE_LIST_FUNCTIONS(list_memory, struct memory_mapping *)

struct task *task_head = NULL;
u64 active_pid = 0;

static void set_current_task(struct task *task) {
  kernel_threads[core_id_get()].current_task = task;
}

struct task *get_current_task(void) {
  return kernel_threads[core_id_get()].current_task;
}

bool task_init(void) {
  task_head = kmalloc(sizeof(struct task));
  if (!task_head) {
    return false;
  }
  task_head->parent = NULL;
  task_head->next = NULL;
  task_head->pid = active_pid;
  task_head->active_kpoll = NULL;
  list_fd_init(&task_head->fds);
  list_memory_init(&task_head->mappings);
  active_pid++;

  task_head->directory = mmu_get_active_directory();

  set_current_task(task_head);

  return true;
}

err_t task_fd_dup2(u64 oldfd, u64 newfd) {
  if (oldfd == newfd) {
    return ERROR_SUCCESS;
  }

  struct task *task = get_current_task();

  struct vfs_fd *fd_ptr;
  GET_FD(oldfd, &fd_ptr);

  (void)task_fd_close(newfd);

  // TODO: Maybe don't do the task_fd_close if this fails?
  TRY(list_fd_set(&task->fds, newfd, fd_ptr));

  fd_ptr->outside_references++;
  return ERROR_SUCCESS;
}

err_t task_fd_pipe(u64 fd[2]) {
  struct vfs_fd *fds[2];
  TRY(pipe(fds));

  if (!list_fd_add(&get_current_task()->fds, fds[0], &fd[0])) {
    vfs_close(fds[0]);
    vfs_close(fds[1]);
    return ERROR_NO_MEMORY;
  }
  if (!list_fd_add(&get_current_task()->fds, fds[1], &fd[1])) {
    // TODO: Cleanup previous fd
    vfs_close(fds[0]);
    vfs_close(fds[1]);
    return ERROR_NO_MEMORY;
  }
  return ERROR_SUCCESS;
}

err_t task_fd_open(u64 *fd, struct sv path, int flags) {
  err_t err;
  struct vfs_fd *fd_ptr = vfs_open(path, flags, &err);
  if (!fd_ptr) {
    hint_assert(ERROR_SUCCESS != err);
    return err;
  }

  if (!list_fd_add(&get_current_task()->fds, fd_ptr, fd)) {
    return ERROR_NO_MEMORY;
  }

  return ERROR_SUCCESS;
}

err_t task_fd_read(u64 fd, void *buffer, u64 count, u64 *out) {
  struct vfs_fd *fd_ptr;
  GET_FD(fd, &fd_ptr);
  return vfs_read(fd_ptr, buffer, count, out);
}

err_t task_fd_write(u64 fd, const void *buffer, u64 count, u64 *out) {
  struct vfs_fd *fd_ptr;
  GET_FD(fd, &fd_ptr);
  return vfs_write(fd_ptr, buffer, count, out);
}

err_t task_lseek(u64 fd, off_t offset, int whence, off_t *out) {
  struct vfs_fd *fd_ptr;
  GET_FD(fd, &fd_ptr);
  return vfs_lseek(fd_ptr, offset, whence, out);
}

err_t task_fd_close(u64 fd) {
  struct vfs_fd *fd_ptr;
  GET_FD(fd, &fd_ptr);
  vfs_close(fd_ptr);
  return ERROR_SUCCESS;
}

struct PML4T {
  uintptr_t physical[512];
  struct PDPT *pdpt[512];
};

// NOTE: This function is called from the assembly function
// `weird_switch` and therefore should not have its interface changed.
void task_create_directory(struct task *task, struct task *parent) {
  task->directory = mmu_clone_directory(parent->directory, &parent->mappings);
  task->tcb.cr3 = (u64)task->directory->physical;
}

void jump_usermode(void(*ring3_function), void *stack);

static err_t allocate(struct memory_mapping *map, void *addr, size_t length,
                      int prot, int flags, int fd, off_t offset, void **out) {
  map->flags = flags;
  // TODO: Handle prot
  (void)prot;
  (void)offset;
  if (flags & MAP_STACK) {
    if (!(flags & MAP_ANONYMOUS) || !(flags & MAP_PRIVATE)) {
      return ERROR_MMAP_INVALID_FLAGS;
    }
    // NOTE: Fall through and performs the MAP_ANONYMOUS call.
  }

  if (flags & MAP_ANONYMOUS) {
    void *ptr;
    TRY(mmu_setup_random_region(addr, length, true, true,
                                MMU_FLAG_RW | MMU_FLAG_USER, &ptr));
    map->fd = NULL;
    map->address = ptr;
    map->length = length;
    if (out) {
      *out = ptr;
      memset(*out, 0, length);
    }
    return ERROR_SUCCESS;
  }
  struct vfs_fd *fd_ptr;
  GET_FD(fd, &fd_ptr);
  map->fd = fd_ptr;

  void *r;
  TRY(vfs_mmap(fd_ptr, addr, length, prot, flags, offset, &r));
  map->address = r;
  map->length = length;

  ASSIGN_PTR(out, r);
  return ERROR_SUCCESS;
}

err_t raw_task_munmap(struct memory_mapping *map) {
  void *address = map->address;
  size_t length = map->length;
  bool deallocate = false;

  map->refs--;
  if (0 == map->refs) {
    deallocate = true;
    kfree(map);
  }
  mmu_unmap_frames(address, length, deallocate);
  return ERROR_SUCCESS;
}

err_t task_munmap(void *addr, size_t length) {
  // TODO: Does length really matter? Should mmaps be able to overlap?
  (void)length;
  struct list_memory_ctx *maps = &get_current_task()->mappings;
  for (u64 j = 0;; j++) {
    struct memory_mapping *map;
    if (!list_memory_get(maps, j, &map)) {
      break;
    }
    if (!map) {
      continue;
    }
    if (map->address <= addr &&
        addr <= (void *)((u8 *)map->address + map->length)) {
      list_memory_remove(maps, j);
      raw_task_munmap(map);
      return ERROR_SUCCESS;
    }
  }
  return ERROR_MMAP_INVALID_MAP;
}

err_t task_mmap(void *addr, size_t length, int prot, int flags, int fd,
                off_t offset, void **out) {
  struct memory_mapping *map = kmalloc(sizeof(struct memory_mapping));
  if (!map) {
    return ERROR_NO_MEMORY;
  }

  map->refs = 1;

  u64 index;
  err_t rc;
  if (ERROR_SUCCESS !=
      (rc = list_memory_add(&get_current_task()->mappings, map, &index))) {
    kfree(map);
    return rc;
  }

  if (ERROR_SUCCESS !=
      (rc = allocate(map, addr, length, prot, flags, fd, offset, out))) {
    kfree(map);
    list_memory_remove(&get_current_task()->mappings, index);
    return rc;
  }
  return ERROR_SUCCESS;
}

static err_t setup_stack(void **out, u64 stack_length, struct sv *args,
                         u32 num_of_args, void **result) {
  void *stack_pointer;
  TRY(task_mmap(NULL, stack_length, PROT_READ | PROT_WRITE,
                MAP_STACK | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0,
                &stack_pointer));
  stack_pointer = (void *)((uintptr_t)stack_pointer + stack_length);
  if (out) {
    *out = stack_pointer;
  }

  uintptr_t ptr = (uintptr_t)stack_pointer;

  char **argv_ptrs = kallocarray(sizeof(char *), num_of_args + 1);
  for (u32 i = 0; i < num_of_args; i++) {
    const char *s = sv_buffer(args[i]);
    size_t l = sv_length(args[i]);
    ptr -= l + 1;
    char *b = (char *)ptr;
    memcpy(b, s, l);
    b[l] = '\0';
    argv_ptrs[i] = b;
  }

  char ***ptrs = kallocarray(sizeof(char **), num_of_args + 1);
  for (u32 i = num_of_args; i > 0; i--) {
    ptr -= sizeof(char *);
    ptrs[i] = (char **)ptr;
    if (i != num_of_args) {
      *(ptrs[i]) = argv_ptrs[i];
    } else {
      *(ptrs[i]) = NULL;
    }
  }

  u64 tmp = ptr;

  // Hacky thing to fix alignment
  ptr -= 0xF * 2;
  ptr &= ~(0xF);

  char *s = (char *)tmp;
  ptr -= sizeof(char **);
  *(char ***)ptr = (char **)s;

  ptr -= sizeof(u64);
  *(int *)ptr = num_of_args;

  if (result) {
    *result = (void *)ptr;
  }
  kfree(argv_ptrs);
  kfree(ptrs);
  return ERROR_SUCCESS;
}

err_t task_exec(struct sv file, struct sv *args, u32 num_of_args) {
  struct list_memory_ctx *maps = &get_current_task()->mappings;
  for (u64 j = 0;; j++) {
    struct memory_mapping *map;
    if (!list_memory_get(maps, j, &map)) {
      break;
    }
    if (!map) {
      continue;
    }
    raw_task_munmap(map);
  }

  mmu_unmap_frames(0, 0xF000000000, true);

  void *program_end;
  void *entry;
  TRY(elf_load_file(file, &program_end, &entry));

  uintptr_t stack_length = 0x5000;
  void *stack_ptr;
  assert(ERROR_SUCCESS ==
         setup_stack(&stack_ptr, stack_length, args, num_of_args, &stack_ptr));

  jump_usermode(entry, (void *)stack_ptr);
  assert(0);
}

err_t task_fork(u64 *pid) {
  struct task *parent = get_current_task();
  assert(parent);

  struct task *task = kmalloc(sizeof(struct task));
  if (!task) {
    return ERROR_NO_MEMORY;
  }

  task->parent = parent;

  hint_assert(!parent->active_kpoll);
  task->active_kpoll = NULL;

  task->pid = active_pid;
  active_pid++;
  list_fd_clone(&task->fds, &parent->fds);
  list_memory_clone(&task->mappings, &parent->mappings);
  // TODO: Have a separate function for this
  for (u64 i = 0; i < task->mappings.length; i++) {
    struct memory_mapping *map;
    assert(list_memory_get(&task->mappings, i, &map));
    if (!map) {
      continue;
    }
    map->refs++;
  }
  for (u64 i = 0; i < task->fds.length; i++) {
    struct vfs_fd *fd;
    assert(list_fd_get(&task->fds, i, &fd));
    if (!fd) {
      continue;
    }
    fd->outside_references++;
  }

  task->next = task_head;
  task_head = task;

  // This function(written in assembly) fixes the execution context for
  // the child. It also calls the function task_create_directory() to
  // create a new directory.
  u64 _pid = weird_switch(task, parent);
  ASSIGN_PTR(pid, _pid);
  return ERROR_SUCCESS;
}

void task_switch(struct task *task) {
  struct task *old = get_current_task();
  set_current_task(task);

  mmu_lazy_set_directory(get_current_task()->directory);
  switch_to_task(old, task);
}

static struct task *task_next(struct task *task) {
  task = task->next;
  if (!task) {
    task = task_head;
  }
  return task;
}

static bool is_halted(struct task *task) {
  struct kpoll *kpoll = task->active_kpoll;
  if (kpoll) {
    lock_acquire(&kpoll->lock);
    if (0 == list_listener_num_entries(&kpoll->updates)) {
      lock_release(&kpoll->lock);
      return true;
    }
    lock_release(&kpoll->lock);
  }
  return false;
}

void task_legacy_switch(void) {
  interrupts_disable();
  struct task *new_task = get_current_task();
  do {
    new_task = task_next(new_task);
  } while (is_halted(new_task));
  if (new_task == get_current_task()) {
    return;
  }
  task_switch(new_task);
}
