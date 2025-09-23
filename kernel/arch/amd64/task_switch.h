#include <task.h>
#include <typedefs.h>

void switch_to_task(struct task *old_task, struct task *new_task);
u64 weird_switch(struct task *task, struct task *parent);
