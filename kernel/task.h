#include <list.h>
#ifndef MEMORY_LIST_DEFINE
#define MEMORY_LIST_DEFINE
struct memory_mapping {
  struct vfs_fd *fd;
  void *address;
  size_t length;
  int flags;
  u32 refs;
};
DEFINE_LIST_STRUCT(list_memory, struct memory_mapping *)
#endif

#ifndef TASK_H
#define TASK_H
#include <error.h>
#include <fs/vfs.h>
#include <mmu.h>
#include <stdbool.h>
#include <sv.h>
#include <sys/kpoll.h>
#include <sys/types.h>
#include <task.h>
#include <typedefs.h>

DEFINE_LIST_STRUCT(list_fd, struct vfs_fd *)

struct tcb {
  u64 rsp;
  u64 cr3;
  u64 rsp0;
} __attribute__((packed));

struct task {
  // NOTE: Assembly code depends upon the TCB being at the start
  struct tcb tcb;
  u64 pid;
  void *program_stop;
  struct list_fd_ctx fds;
  struct list_memory_ctx mappings;
  struct mmu_directory *directory;
  struct kpoll *active_kpoll;
  struct task *parent;
  struct task *next;
};

#define GET_FD(a, b)                                                           \
  if (!list_fd_get(&get_current_task()->fds, (a), (b))) {                      \
    return ERROR_INVALID_FD;                                                   \
  }                                                                            \
  if (!(*(b))) {                                                               \
    return ERROR_INVALID_FD;                                                   \
  }

bool task_init(void);
err_t task_fork(u64 *pid);
void task_legacy_switch(void);
void task_exec(struct sv file, struct sv *args, u32 num_of_args);
err_t task_fd_open(u64 *fd, struct sv path, int flags);
err_t task_fd_write(u64 fd, const void *buffer, u64 count, u64 *out);
err_t task_fd_read(u64 fd, void *buffer, u64 count, u64 *out);
err_t task_fd_close(u64 fd);
void *task_sbrk(uintptr_t increment);
err_t task_lseek(u64 fd, off_t offset, int whence, off_t *out);
err_t task_mmap(void *addr, size_t length, int prot, int flags, int fd,
                off_t offset, void **out);
err_t task_fd_dup2(u64 oldfd, u64 newfd);
err_t task_fd_pipe(u64 fd[2]);
struct task *get_current_task(void);
#endif // TASK_H
