#include <arch/amd64/task_switch.h>
#include <assert.h>
#include <elf.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <stddef.h>
#include <task.h>

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
  active_pid++;

  task_head->directory = mmu_get_active_directory();

  task_current = task_head;

  return true;
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

void task_exec(struct sv file) {
  // TODO: Deallocate userland
  void *program_end;
  void *entry = elf_load_file(file, &program_end);
  if (!entry) {
    return;
  }

  size_t stack_length = 0x2000;
  void *stack_ptr = (void *)((uintptr_t)program_end +
                             /*GUARD PAGE*/ PAGE_SIZE * 2 + stack_length);
  mmu_allocate_region(stack_ptr, stack_length, MMU_FLAG_RW | MMU_FLAG_USER);

  jump_usermode(entry, (void *)stack_ptr);
  assert(0);
}

u64 task_fork(bool *err) {
  PTR_ASSIGN(err, false);

  struct task *parent = task_current;
  assert(parent);

  struct task *task = kmalloc(sizeof(struct task));
  if (!task) {
    PTR_ASSIGN(err, true);
    return false;
  }
  task->pid = active_pid;
  active_pid++;

  task->next = task_head;
  task_head = task;

  return weird_switch(task, parent);
}

void task_switch(struct task *task) {
  struct task *old = task_current;
  task_current = task;

  mmu_lazy_set_directory(task_current->directory);
  kprintf("Swithcing to cr3: %x\n", task_current->directory->physical);
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
