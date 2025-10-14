#include <arch/amd64/task_switch.h>
#include <assert.h>
#include <elf.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <log.h>
#include <stddef.h>
#include <task.h>

DEFINE_LIST_FUNCTIONS(list_fd, struct vfs_fd *)

struct task *task_head = NULL;
struct task *task_current = NULL;
u64 active_pid = 0;

bool task_init(void) {
  task_head = kmalloc(sizeof(struct task));
  if (!task_head) {
    return false;
  }
  task_head->next = NULL;
  task_head->pid = active_pid;
  list_fd_init(&task_head->fds);
  active_pid++;

  task_head->directory = mmu_get_active_directory();

  task_current = task_head;

  return true;
}

err_t task_fd_open(u64 *fd, struct sv path, int flags) {
  err_t err;
  struct vfs_fd *fd_ptr = vfs_open(path, flags, &err);
  if (!fd_ptr) {
    return err;
  }

  if (!list_fd_add(&task_current->fds, fd_ptr, fd)) {
    return ERROR_NO_MEMORY;
  }

  return ERROR_SUCCESS;
}

err_t task_fd_write(int fd, const void *buffer, u64 count, u64 *out) {
  struct vfs_fd *fd_ptr;
  list_fd_get(&task_current->fds, fd, &fd_ptr);
  if (!fd_ptr) {
    return ERROR_INVALID_FD;
  }
  err_t err;
  u64 r = vfs_write(fd_ptr, buffer, count, &err);
  ASSIGN_PTR(out, r);
  return err;
}

void task_fd_close(u64 fd) {
  (void)fd;
  klog(LOG_NOTE, "TODO: Task_close");
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

void *task_sbrk(u64 increment) {
  uintptr_t a = (uintptr_t)task_current->program_stop;
  mmu_allocate_region((void *)a, increment, MMU_FLAG_RW | MMU_FLAG_USER);

  task_current->program_stop = (void *)(a + align_up(increment, PAGE_SIZE));
  return (void *)a;
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
  task_current->program_stop = stack_ptr;
  task_current->program_stop += PAGE_SIZE; // Guard page
  mmu_allocate_region(stack_ptr - stack_length, stack_length,
                      MMU_FLAG_RW | MMU_FLAG_USER);

  jump_usermode(entry, (void *)stack_ptr);
  assert(0);
}

err_t task_fork(u64 *pid) {
  struct task *parent = task_current;
  assert(parent);

  struct task *task = kmalloc(sizeof(struct task));
  if (!task) {
    return ERROR_NO_MEMORY;
  }
  task->pid = active_pid;
  active_pid++;
  list_fd_init(&task->fds);

  task->next = task_head;
  task_head = task;

  ASSIGN_PTR(pid, weird_switch(task, parent));
  return ERROR_SUCCESS;
}

void task_switch(struct task *task) {
  struct task *old = task_current;
  task_current = task;

  mmu_lazy_set_directory(task_current->directory);
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
  struct task *new_task = task_next(task_current);
  if (new_task == task_current) {
    return;
  }
  task_switch(new_task);
}
