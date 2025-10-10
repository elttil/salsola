#ifndef TASK_H
#define TASK_H
#include <mmu.h>
#include <stdbool.h>
#include <sv.h>
#include <typedefs.h>
#include <error.h>

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
err_t task_fork(u64 *pid);
void task_legacy_switch(void);
void task_exec(struct sv file);
#endif // TASK_H
