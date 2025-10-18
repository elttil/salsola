#include "multiboot2.h"
#include <mmu.h>
#include <task.h>
#include <typedefs.h>

struct kernel_thread {
  struct mmu_directory *active_directory;
  struct task *current_task;
};

#define MAX_CORES 64
extern struct kernel_thread kernel_threads[MAX_CORES];

void smp_init(struct multiboot_tag *tags);
u8 core_id_get(void);
