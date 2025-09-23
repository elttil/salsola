#ifndef TASK_H
#define TASK_H
#include <mmu.h>
#include <typedefs.h>

struct tcb {
  u64 rsp;
  u64 cr3;
  u64 rsp0;
} __attribute__((packed));

struct task {
  // NOTE: Assembly code depends upon the TCB being at the start
  struct tcb tcb;
  u64 pid;
  struct mmu_directory *directory;
  struct task *next;
} __attribute__((packed));

bool task_init(void);
u64 task_fork(bool *err);
void task_legacy_switch(void);
#endif // TASK_H
