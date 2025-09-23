#include <arch/amd64/task_switch.h>
#include <assert.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <stddef.h>
#include <task.h>

struct task *task_head = NULL;
struct task *task_current = NULL;

bool task_init(void) {
  task_head = kmalloc(sizeof(struct task));
  if (!task_head) {
    return false;
  }
  task_head->next = NULL;

  task_head->directory = mmu_get_active_directory();

  task_current = task_head;

  return true;
}

void task_create_directory(struct task *task, struct task *parent) {
  task->directory = mmu_clone_directory(parent->directory);
  task->tcb.cr3 = (u64)task->directory->physical;
}

bool task_create(void) {
  struct task *parent = task_current;
  assert(parent);

  struct task *task = kmalloc(sizeof(struct task));
  if (!task) {
    return false;
  }

  task->next = task_head;
  task_head = task;

  return weird_switch(task, parent);
}

void task_switch(struct task *task) {
  struct task *old = task_current;
  task_current = task;
  switch_to_task(old, task);
}

struct task *task_next(struct task *task) {
  task = task->next;
  if (!task) {
    task = task_head;
  }
  return task;
}

void task_legacy_switch(void) {
  struct task *new_task = task_next(task_current);
  task_switch(new_task);
}

bool task_fork(void) {
  return task_create();
}
