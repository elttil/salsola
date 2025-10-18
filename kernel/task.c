#include <arch/amd64/idt.h>
#include <arch/amd64/smp.h>
#include <arch/amd64/task_switch.h>
#include <assert.h>
#include <elf.h>
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

static struct task *get_current_task(void) {
  return kernel_threads[core_id_get()].current_task;
}

bool task_init(void) {
  task_head = kmalloc(sizeof(struct task));
  if (!task_head) {
    return false;
  }
  task_head->next = NULL;
  task_head->pid = active_pid;
  list_fd_init(&task_head->fds);
  list_memory_init(&task_head->mappings);
  active_pid++;

  task_head->directory = mmu_get_active_directory();

  set_current_task(task_head);

  return true;
}

err_t task_fd_open(u64 *fd, struct sv path, int flags) {
  err_t err;
  struct vfs_fd *fd_ptr = vfs_open(path, flags, &err);
  if (!fd_ptr) {
    return err;
  }

  if (!list_fd_add(&get_current_task()->fds, fd_ptr, fd)) {
    return ERROR_NO_MEMORY;
  }

  return ERROR_SUCCESS;
}

err_t task_fd_read(u64 fd, void *buffer, u64 count, u64 *out) {
  struct vfs_fd *fd_ptr;
  list_fd_get(&get_current_task()->fds, fd, &fd_ptr);
  if (!fd_ptr) {
    return ERROR_INVALID_FD;
  }
  err_t err;
  u64 r = vfs_read(fd_ptr, buffer, count, &err);
  ASSIGN_PTR(out, r);
  return err;
}

err_t task_fd_write(u64 fd, const void *buffer, u64 count, u64 *out) {
  struct vfs_fd *fd_ptr;
  list_fd_get(&get_current_task()->fds, fd, &fd_ptr);
  if (!fd_ptr) {
    return ERROR_INVALID_FD;
  }
  err_t err;
  u64 r = vfs_write(fd_ptr, buffer, count, &err);
  ASSIGN_PTR(out, r);
  return err;
}

err_t task_lseek(u64 fd, off_t offset, int whence, off_t *out) {
  struct vfs_fd *fd_ptr;
  list_fd_get(&get_current_task()->fds, fd, &fd_ptr);
  if (!fd_ptr) {
    return ERROR_INVALID_FD;
  }
  return vfs_lseek(fd_ptr, offset, whence, out);
}

err_t task_fd_close(u64 fd) {
  struct vfs_fd *fd_ptr;
  list_fd_get(&get_current_task()->fds, fd, &fd_ptr);
  if (!fd_ptr) {
    return ERROR_INVALID_FD;
  }
  vfs_close(fd_ptr);
  return ERROR_SUCCESS;
}

struct PML4T {
  uintptr_t physical[512];
  struct PDPT *pdpt[512];
};

void task_create_directory(struct task *task, struct task *parent) {
  task->directory = mmu_clone_directory(parent->directory);
  task->tcb.cr3 = (u64)task->directory->physical;
}

void jump_usermode(void(*ring3_function), void *stack);

static err_t allocate(struct memory_mapping *map, void *addr, size_t length,
                      int prot, int flags, int fd, off_t offset, void **out) {
  map->flags = flags;
  // TODO: Handle prot
  (void)prot;
  (void)offset;
  if (flags & MAP_ANONYMOUS) {
    void *ptr;
    TRY(mmu_setup_random_region(addr, length, true, true,
                                MMU_FLAG_RW | MMU_FLAG_USER, &ptr));
    map->fd = NULL;
    map->address = out;
    map->length = length;
    if (out) {
      *out = ptr;
      memset(*out, 0, length);
    }
    return ERROR_SUCCESS;
  }
  struct vfs_fd *fd_ptr;
  list_fd_get(&get_current_task()->fds, fd, &fd_ptr);
  if (!fd_ptr) {
    return ERROR_INVALID_FD;
  }
  map->fd = fd_ptr;

  void *r;
  TRY(vfs_mmap(fd_ptr, addr, length, prot, flags, offset, &r));
  map->address = r;
  map->length = length;

  ASSIGN_PTR(out, r);
  return ERROR_SUCCESS;
}

err_t task_mmap(void *addr, size_t length, int prot, int flags, int fd,
                off_t offset, void **out) {
  struct memory_mapping *map = kmalloc(sizeof(struct memory_mapping));

  if (!map) {
    return ERROR_NO_MEMORY;
  }

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
    list_memory_set(&get_current_task()->mappings, index, NULL);
    return rc;
  }
  return ERROR_SUCCESS;
}

void task_exec(struct sv file) {
  // TODO: Deallocate userland
  void *program_end;
  void *entry = elf_load_file(file, &program_end);
  if (!entry) {
    return;
  }

  uintptr_t stack_diff = PAGE_SIZE * 10;
  uintptr_t stack_length = 0x5000;
  void *stack_ptr = (void *)((uintptr_t)program_end +
                             /*GUARD PAGE*/ stack_diff + stack_length);
  stack_ptr = align_up(stack_ptr, PAGE_SIZE);
  get_current_task()->program_stop = stack_ptr;
  get_current_task()->program_stop += PAGE_SIZE; // Guard page
  mmu_allocate_region(stack_ptr - stack_length, stack_length,
                      MMU_FLAG_RW | MMU_FLAG_USER);

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
  task->pid = active_pid;
  active_pid++;
  list_fd_init(&task->fds);
  list_memory_init(&task->mappings);

  task->next = task_head;
  task_head = task;

  ASSIGN_PTR(pid, weird_switch(task, parent));
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

void task_legacy_switch(void) {
  interrupts_disable();
  struct task *new_task = task_next(get_current_task());
  if (new_task == get_current_task()) {
    return;
  }
  task_switch(new_task);
}
