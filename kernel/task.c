#include <arch/amd64/idt.h>
#include <arch/amd64/smp.h>
#include <arch/amd64/task_switch.h>
#include <assert.h>
#include <elf.h>
#include <fcntl.h>
#include <fs/pipe.h>
#include <kmalloc.h>
#include <log.h>
#include <stddef.h>
#include <sys/mman.h>
#include <task.h>
#include <timer.h>

DEFINE_LIST_FUNCTIONS(list_fd, struct vfs_fd *);
DEFINE_LIST_FUNCTIONS(list_memory, struct memory_mapping *);

lock_t task_list_lock;
struct task *task_head = NULL;
struct task *pid1_task = NULL;
u64 active_pid = 0;

void task_switch(struct task *task);
WARN_UNUSED err_t raw_task_munmap(struct memory_mapping *map);
WARN_UNUSED static struct task *task_next(struct task *task);

struct task *get_task_head(void) {
  return task_head;
}

static void set_current_task(struct task *task) {
  kernel_threads[core_id_get()].current_task = task;
}

struct task *get_current_task(void) {
  return kernel_threads[core_id_get()].current_task;
}

void task_delete_maps(struct task *task) {
  struct list_memory_ctx *maps = &task->mappings;
  for (u64 j = 0;; j++) {
    struct memory_mapping *map;
    if (!list_memory_get(maps, j, &map)) {
      break;
    }
    if (!map) {
      continue;
    }
    UNUSED(raw_task_munmap(map));
    list_memory_remove(maps, j);
  }

  mmu_unmap_frames(0, 0xF000000000, true);
}

bool task_init(void) {
  lock_release(&task_list_lock);

  lock_acquire(&task_list_lock);
  err_t err = kmalloc2((void **)&task_head, sizeof(struct task));
  if (ERROR_SUCCESS != err) {
    lock_release(&task_list_lock);
    return false;
  }
  task_head->children = NULL;
  lock_release(&task_head->death_lock);
  lock_release(&task_head->child_list_lock);
  task_head->in_use = true;
  task_head->parent = NULL;
  task_head->next = NULL;
  task_head->pid = active_pid;
  task_head->active_kpoll = NULL;
  task_head->program_name = sv_clone(C_TO_SV("KERNEL"));
  task_head->is_dead = false;
  task_head->namespace = NULL;
  task_head->sleep_until = 0;

  lock_release(&task_head->cwd_lock);
  sb_init(&task_head->cwd);
  assert(sb_append_char(&task_head->cwd, '/')); // TODO: OOM

  list_fd_init(&task_head->fds);
  list_memory_init(&task_head->mappings);
  active_pid++;

  task_head->directory = mmu_get_active_directory();

  lock_release(&task_list_lock);

  set_current_task(task_head);

  return true;
}

err_t task_get_from_pid(u64 pid, struct task **out) {
  lock_acquire(&task_list_lock);
  struct task *p = task_head;
  for (; p; p = p->next) {
    if (p->pid == pid) {
      if (out) {
        *out = p;
        p->outside_reference++;
      }
      lock_release(&task_list_lock);
      return ERROR_SUCCESS;
    }
  }
  lock_release(&task_list_lock);
  return ERROR_TASK_NOT_FOUND;
}

err_t task_fd_dup2(u64 oldfd, u64 newfd) {
  if (oldfd == newfd) {
    return ERROR_SUCCESS;
  }

  struct task *task = get_current_task();

  struct vfs_fd *fd_ptr;
  GET_FD(oldfd, &fd_ptr);

  UNUSED(task_fd_close(newfd));

  // TODO: Maybe don't do the task_fd_close if this fails?
  TRY(list_fd_set(&task->fds, newfd, fd_ptr));

  struct vfs_fd *fd_ptr2;
  GET_FD(newfd, &fd_ptr2);
  assert(fd_ptr2 == fd_ptr);

  fd_ptr->references++;
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

void task_set_wait(struct vfs_fd *fd, int flag) {
  struct task *task = get_current_task();
  task->wait.fd = fd;
  task->wait.flag = flag;
  task_legacy_switch();
}

bool path_cleaner(struct sb *out, struct sv path, u16 *skip_ptr) {
  u16 skip = 0;
  if (skip_ptr) {
    skip = *skip_ptr;
  }
  struct sv p = path;
  for (; sv_length(p) > 0;) {
    struct sv rev_path = sv_end_split_delim(p, &p, '/');
    sv_try_eat(rev_path, &rev_path, C_TO_SV("/"));
    if (0 == sv_length(rev_path) || sv_eq(rev_path, C_TO_SV("."))) {
      // NOP
      continue;
    }
    if (sv_eq(rev_path, C_TO_SV(".."))) {
      skip++;
      continue;
    }
    if (skip > 0) {
      skip--;
      continue;
    }
    assert(sb_prepend_sv(out, rev_path));
    assert(sb_prepend_sv(out, C_TO_SV("/")));
  }
  ASSIGN_PTR(skip_ptr, skip);
  return true;
}

bool path_open(struct sb *out, struct sv directory, struct sv path) {
  sb_init(out);
  if (sv_partial_eq(path, C_TO_SV("/"))) {
    path_cleaner(out, path, NULL);
    if (0 == sv_length(SB_TO_SV(*out))) {
      sb_append(out, "/");
    }
    return true;
  }

  u16 skip = 0;
  assert(path_cleaner(out, path, &skip));
  assert(path_cleaner(out, directory, &skip));

  if (0 == sv_length(SB_TO_SV(*out))) {
    sb_append(out, "/");
  }
  return true;
}

err_t task_chdir(struct sv path) {
  struct task *task = get_current_task();
  lock_acquire(&task->cwd_lock);

  struct sv cwd = SB_TO_SV(task->cwd);

  struct sb new_cwd;
  assert(path_open(&new_cwd, cwd, path));

  struct vfs_fd *fd = vfs_open(SB_TO_SV(new_cwd), 0, NULL);
  if (!fd) {
    lock_release(&task->cwd_lock);
    return ERROR_NO_FILE;
  }
  if (VFS_TYPE_DIRECTORY != fd->type) {
    vfs_close(fd);
    lock_release(&task->cwd_lock);
    return ERROR_NOT_A_DIRECTORY;
  }

  vfs_close(fd);

  sb_free(&task->cwd);
  sb_clone(&task->cwd, &new_cwd);
  sb_free(&new_cwd);

  lock_release(&task->cwd_lock);

  return ERROR_SUCCESS;
}

err_t task_getcwd(char *buffer, size_t size) {
  if (0 == size) {
    return ERROR_BUFFER_TOO_SMALL;
  }
  struct task *task = get_current_task();
  lock_acquire(&task->cwd_lock);
  struct sv cwd = SB_TO_SV(task->cwd);
  if (sv_length(cwd) > size - 1) {
    lock_release(&task->cwd_lock);
    return ERROR_BUFFER_TOO_SMALL;
  }
  memcpy(buffer, sv_buffer(cwd), sv_length(cwd));
  buffer[sv_length(cwd)] = '\0';
  lock_release(&task->cwd_lock);
  return ERROR_SUCCESS;
}

err_t task_fd_open(u64 *fd, struct sv path, int flags) {
  struct task *task = get_current_task();

  lock_acquire(&task->cwd_lock);
  struct sb out;
  assert(path_open(&out, SB_TO_SV(task->cwd), path)); // TODO: OOM
  lock_release(&task->cwd_lock);

  err_t err;
  struct vfs_fd *fd_ptr = vfs_open(path, flags, &err);
  if (!fd_ptr) {
    hint_assert(ERROR_SUCCESS != err);
    return err;
  }

  if (!list_fd_add_or_replace_previous_null(&task->fds, fd_ptr, fd)) {
    return ERROR_NO_MEMORY;
  }

  return ERROR_SUCCESS;
}

err_t task_fd_getdent(u64 fd, struct vfs_dirent *dirp, size_t dir_entry_size,
                      u64 nentries, u64 *rc) {
  struct vfs_fd *fd_ptr;
  GET_FD(fd, &fd_ptr);
  return vfs_getdent(fd_ptr, dirp, dir_entry_size, nentries, rc);
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

static struct task *get_dead_child(struct task *task, struct vfs_fd *fd) {
  lock_acquire(&task->child_list_lock);

  struct child_list **prev_next = &task->children;
  struct child_list *entry = task->children;
  for (; entry; prev_next = &entry->next, entry = entry->next) {
    struct task *t = entry->task;
    lock_acquire(&t->death_lock);
    if (!t->is_dead) {
      lock_release(&t->death_lock);
      continue;
    }
    lock_release(&t->death_lock);

    if (fd == NULL || fd->internal_object == t) {
      *prev_next = entry->next;

      lock_release(&task->child_list_lock);
      return t;
    }
  }

  lock_release(&task->child_list_lock);
  return NULL;
}

err_t task_waitfd(int fd, u8 *exit_code, pid_t *pid) {
  struct task *task = get_current_task();
  task->wait_for_child = true;

  if (-1 != fd) {
    struct vfs_fd *fd_ptr;
    GET_FD(fd, &fd_ptr);
    if (VFS_UNIQUE_TYPE_PROCESS != fd_ptr->internal_object_type) {
      return ERROR_NOT_A_PROCESS;
    }
    task->wait_child_fdptr = fd_ptr;
  } else {
    task->wait_child_fdptr = NULL;
  }

  struct task *child;
  for (; NULL == (child = get_dead_child(task, task->wait_child_fdptr));) {
    task_legacy_switch();
  }
  ASSIGN_PTR(exit_code, child->exit_code);
  ASSIGN_PTR(pid, child->pid);

  // TODO: Actually kill and gut the child.
  if (task_head == child) {
    task_head = child->next;
  }
  assert(task_head);

  {
    struct task *p = task_head;
    for (; p; p = p->next) {
      if (p->next == child) {
        p->next = child->next;
        break;
      }
    }
  }

  child->next = NULL;

  return ERROR_SUCCESS;
}

void task_exit(u8 exit_code) {
  struct task *task = get_current_task();
  assert(task != pid1_task);

  lock_acquire(&task_list_lock);

  lock_acquire(&task->death_lock);
  task->is_dead = true;
  task->exit_code = exit_code;
  lock_release(&task->death_lock);

  struct task *new_task = task_head;
  for (;;) {
    new_task = task_next(new_task);

    if (new_task->is_dead) {
      goto retry;
    }

    if (new_task == get_current_task()) {
      goto retry;
    }

    if (new_task->in_use) {
      goto retry;
    }
    break;
  retry:
    lock_release(&task_list_lock);
    __asm__("hlt");
    lock_acquire(&task_list_lock);
  }

  assert(!new_task->in_use && !new_task->is_dead);

  task->in_use = false;
  new_task->in_use = true;

  {
    lock_acquire(&task->child_list_lock);
    struct child_list *p = task->children;
    for (; p;) {
      lock_acquire(&p->task->death_lock);
      p->task->parent = pid1_task;
      lock_release(&p->task->death_lock);
      task->children = p->next;
      p = p->next;
    }
    lock_release(&task->child_list_lock);
  }

  lock_release(&task_list_lock);

  task_delete_maps(task);

  for (u64 i = 0; i < task->fds.length; i++) {
    struct vfs_fd *fd;
    assert(list_fd_get(&task->fds, i, &fd));
    if (!fd) {
      continue;
    }
    vfs_close(fd);
  }

  task_switch(new_task);
}

err_t task_lseek(u64 fd, off_t offset, int whence, off_t *out) {
  struct vfs_fd *fd_ptr;
  GET_FD(fd, &fd_ptr);
  return vfs_lseek(fd_ptr, offset, whence, out);
}

err_t task_fd_truncate(u64 fd, u64 length) {
  struct vfs_fd *fd_ptr;
  GET_FD(fd, &fd_ptr);
  return vfs_truncate(fd_ptr, length);
}

err_t task_fd_close(u64 fd) {
  struct vfs_fd *fd_ptr;
  GET_FD(fd, &fd_ptr);
  vfs_close(fd_ptr);
  list_fd_remove(&get_current_task()->fds, fd);

  u64 index;
  list_fd_find_index_by_value(&get_current_task()->fds, &index, NULL);
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

WARN_UNUSED static err_t allocate(struct memory_mapping *map, void *addr,
                                  size_t length, int prot, int flags, int fd,
                                  off_t offset, void **out) {
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
    TRY(mmu_setup_random_region(
        addr, length, true, true,
        MMU_FLAG_RW | MMU_FLAG_USER | MMU_FLAG_FAKE_ALLOCATION, &ptr));
    map->fd = NULL;
    map->address = ptr;
    map->length = length;
    if (out) {
      *out = ptr;
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

WARN_UNUSED err_t raw_task_munmap(struct memory_mapping *map) {
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

WARN_UNUSED err_t task_munmap(void *addr, size_t length) {
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
      assert(ERROR_SUCCESS == raw_task_munmap(map));
      return ERROR_SUCCESS;
    }
  }
  return ERROR_MMAP_INVALID_MAP;
}

WARN_UNUSED err_t task_mmap(void *addr, size_t length, int prot, int flags,
                            int fd, off_t offset, void **out) {
  struct memory_mapping *map;
  err_t err = kmalloc2((void **)&map, sizeof(struct memory_mapping));
  if (ERROR_SUCCESS != err) {
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

static void *add_to_stack(void *top, struct sv *args, u32 num_of_args) {
  uintptr_t ptr = (uintptr_t)top;

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
  for (i32 i = num_of_args; i >= 0; i--) {
    ptr -= sizeof(char *);
    ptrs[i] = (char **)ptr;
    if (i != (i32)num_of_args) {
      *(ptrs[i]) = argv_ptrs[i];
    } else {
      *(ptrs[i]) = NULL;
    }
  }

  kfree(argv_ptrs);
  kfree(ptrs);
  return (void *)ptr;
}

WARN_UNUSED static err_t setup_stack(void **out, u64 stack_length,
                                     struct sv *args, u32 num_of_args,
                                     struct sv *envs, u32 num_of_envs,
                                     void **result) {
  void *stack_pointer;
  TRY(task_mmap(NULL, stack_length, PROT_READ | PROT_WRITE,
                MAP_STACK | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0,
                &stack_pointer));
  stack_pointer = (void *)((uintptr_t)stack_pointer + stack_length);
  if (out) {
    *out = stack_pointer;
  }

  uintptr_t ptr = (uintptr_t)stack_pointer;

  (void)envs;
  (void)num_of_envs;
  ptr = (uintptr_t)add_to_stack((void *)ptr, envs, num_of_envs);
  u64 envp_pointer = ptr;

  ptr = (uintptr_t)add_to_stack((void *)ptr, args, num_of_args);
  u64 argv_pointer = ptr;

  // Hacky thing to fix alignment
  ptr -= 0xF * 2;
  ptr &= ~(0xF);

  ptr -= 0x8;
  ptr -= sizeof(char **);
  *(char ***)ptr = (char **)envp_pointer;

  char *s = (char *)argv_pointer;
  ptr -= sizeof(char **);
  *(char ***)ptr = (char **)s;

  ptr -= sizeof(u64);
  *(int *)ptr = num_of_args;

  if (result) {
    *result = (void *)ptr;
  }
  return ERROR_SUCCESS;
}

err_t task_exec(struct sv file, struct sv *args, u32 num_of_args,
                struct sv *envs, u32 num_of_envs) {
  struct vfs_fd *fd;
  TRY(elf_open(file, &fd));

  struct task *task = get_current_task();
  task_delete_maps(task);

  task->program_name = sv_clone(file);

  void *program_end;
  void *entry;
  err_t err = elf_load_file(fd, &program_end, &entry);
  if (ERROR_SUCCESS != err) {
    // FIXME: What do we do here?
    assert(0);
  }
  vfs_close(fd);

  uintptr_t stack_length = 0x5000;
  void *stack_ptr;
  assert(ERROR_SUCCESS == setup_stack(&stack_ptr, stack_length, args,
                                      num_of_args, envs, num_of_envs,
                                      &stack_ptr));

  jump_usermode(entry, (void *)stack_ptr);
  assert(0);
  return ERROR_SUCCESS;
}

struct vfs_fd *task_find_namespace_override(struct sv path) {
  struct task *task = get_current_task();
  struct namespace_override *o = task->namespace;
  for(;o;o = o->next) {
	if(sv_eq(o->path, path)) { o->fd->references++; return o->fd; }
  }
 return NULL;
}

static err_t add_namespace_override(struct task *task, struct sv path, struct vfs_fd *fd) {
  struct namespace_override *n;
  TRY(kmalloc2((void **)&n, sizeof(*n)));
  // TODO: Fix OOM
  n->path = sv_clone(path);
  assert(fd);
  n->fd = fd;
  n->fd->references++;
  n->next = task->namespace;
  task->namespace = n;
  return ERROR_SUCCESS;
}

err_t task_add_namespace_override(struct sv path, u64 fd) {
  struct vfs_fd *fd_ptr;
  GET_FD(fd, &fd_ptr);
  return add_namespace_override(get_current_task(), path, fd_ptr);
}

err_t task_fork(u64 *pid) {
  struct task *parent = get_current_task();
  assert(parent);

  struct task *task;
  err_t err = kmalloc2((void **)&task, sizeof(struct task));
  if (ERROR_SUCCESS != err) {
    return ERROR_NO_MEMORY;
  }

  lock_release(&task->death_lock);
  lock_release(&task->child_list_lock);

  task->in_use = false;
  task->parent = parent;
  task->children = NULL;
  task->program_name = sv_clone(parent->program_name);
  task->is_dead = false;
  task->sleep_until = 0;

  task->namespace = NULL;
  struct namespace_override *o = parent->namespace;
  for(;o;o = o->next) {
	add_namespace_override(task, o->path, o->fd);
  }

  lock_acquire(&parent->cwd_lock);
  assert(sb_clone(&task->cwd, &parent->cwd)); // TODO: OOM
  lock_release(&parent->cwd_lock);
  lock_release(&task->cwd_lock);

  hint_assert(!parent->active_kpoll);
  task->active_kpoll = NULL;

  task->pid = active_pid;
  if (1 == task->pid && !pid1_task) {
    pid1_task = task;
  }
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
    fd->references++;
  }

  lock_acquire(&task_list_lock);

  struct child_list *entry = kmalloc(sizeof(struct child_list));
  entry->task = task;
  lock_acquire(&task->parent->child_list_lock);
  entry->next = task->parent->children;
  task->parent->children = entry;
  lock_release(&task->parent->child_list_lock);

  task->next = task_head;
  task_head = task;

  // This function(written in assembly) fixes the execution context for
  // the child. It also calls the function task_create_directory() to
  // create a new directory.
  u64 _pid = weird_switch(task, parent);

  lock_release(&task_list_lock);
  ASSIGN_PTR(pid, _pid);
  return ERROR_SUCCESS;
}

void task_switch(struct task *task) {
  struct task *old = get_current_task();
  set_current_task(task);
  assert(task != old);

  mmu_lazy_set_directory(get_current_task()->directory);

  lock_acquire(&task_list_lock);
  if (old) {
    old->in_use = false;
  }
  switch_to_task(old, task);
  lock_release(&task_list_lock);
}

WARN_UNUSED static struct task *task_next(struct task *task) {
  task = task->next;
  if (!task) {
    task = task_head;
  }
  return task;
}

WARN_UNUSED static bool is_halted(struct task *task) {
  struct kpoll *kpoll = task->active_kpoll;
  if (kpoll) {
    lock_acquire(&kpoll->lock);
    if (0 == list_listener_num_entries(&kpoll->updates)) {
      lock_release(&kpoll->lock);
      return true;
    }
    lock_release(&kpoll->lock);
  }
  if (task->wait.fd) {
    struct vfs_fd *fd = task->wait.fd;
    if (TASK_WAIT_READ == task->wait.flag && !fd->data.can_read) {
      return true;
    }
    if (TASK_WAIT_WRITE == task->wait.flag && !fd->data.can_write) {
      return true;
    }
    task->wait.fd = NULL;
  }
  return false;
}

err_t task_fcntl(int fd, int cmd, int arg) {
  struct vfs_fd *fd_ptr;
  GET_FD(fd, &fd_ptr);

  // TODO: Do more
  if (F_SETFL == cmd) {
    if (arg == O_NONBLOCK) {
      fd_ptr->is_blocking = false;
      return ERROR_SUCCESS;
    }
  }
  return ERROR_FCNTL_INVALID_FLAGS;
}

void task_msleep(u64 ms) {
  u64 current = timer_get_ms();
  get_current_task()->sleep_until = current + ms;
  task_legacy_switch();
}

void task_legacy_switch(void) {
  lock_acquire(&task_list_lock);

  u64 current_time = timer_get_ms();

  struct task *new_task = get_current_task();
  for (size_t i = 0;;) {
    new_task = task_next(new_task);

    if (new_task->is_dead) {
      continue;
    }

    if (new_task->sleep_until > current_time) {
      continue;
    }

    if (new_task == get_current_task()) {
      current_time = timer_get_ms();
      if (i < 10) {
        i++;
        continue;
      }
      break;
    }

    if (0 == new_task->pid) {
      continue;
    }

    if (new_task->in_use) {
      continue;
    }
    if (is_halted(new_task)) {
      continue;
    }
    break;
  }

  if (new_task == get_current_task()) {
    lock_release(&task_list_lock);
    return;
  }
  assert(!new_task->in_use && !new_task->is_dead);

  new_task->in_use = true;

  lock_release(&task_list_lock);

  task_switch(new_task);
}

void task_new_core_init(void) {
  struct task *new_task = task_head;
  for (;;) {
  redo:
    lock_acquire(&task_list_lock);
    new_task = task_head;

    for (;;) {
      new_task = task_next(new_task);

      if (new_task == task_head) {
        lock_release(&task_list_lock);
        goto redo;
      }

      if (new_task->is_dead) {
        continue;
      }

      if (new_task->in_use) {
        continue;
      }
      break;
    }
    assert(!new_task->in_use && !new_task->is_dead);

    new_task->in_use = true;

    lock_release(&task_list_lock);
    break;
  }

  task_switch(new_task);
}
