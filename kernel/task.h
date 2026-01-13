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
#include <sb.h>
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

#define TASK_WAIT_READ 1
#define TASK_WAIT_WRITE 2

struct wait {
  struct vfs_fd *fd;
  int flag;
};

struct environment_variable {
  struct sv key;
  struct sb value;
  bool is_used;
  size_t open_ref_count;
  lock_t lock;
};

struct child_list {
  struct task *task;
  struct child_list *next;
};

struct task {
  // NOTE: Assembly code depends upon the TCB being at the start
  struct tcb tcb;
  u64 pid;
  void *program_stop;

  bool in_use;

  u64 outside_reference;
  lock_t variable_lock;
  struct environment_variable *variables;
  size_t variables_num;
  size_t variables_capacity;

  struct sv program_name;
  struct list_fd_ctx fds;
  struct list_memory_ctx mappings;
  struct mmu_directory *directory;
  struct kpoll *active_kpoll;
  struct wait wait;

  struct child_list *children;

  lock_t death_lock;
  bool is_dead;
  u8 exit_code;
  struct task *parent;
  // end of death_lock

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
WARN_UNUSED err_t task_fork(u64 *pid);
void task_legacy_switch(void);
WARN_UNUSED err_t task_exec(struct sv file, struct sv *args, u32 num_of_args);
WARN_UNUSED err_t task_fd_open(u64 *fd, struct sv path, int flags);
WARN_UNUSED err_t task_fd_write(u64 fd, const void *buffer, u64 count,
                                u64 *out);
WARN_UNUSED err_t task_fd_read(u64 fd, void *buffer, u64 count, u64 *out);
WARN_UNUSED err_t task_fd_close(u64 fd);
void *task_sbrk(uintptr_t increment);
WARN_UNUSED err_t task_lseek(u64 fd, off_t offset, int whence, off_t *out);
WARN_UNUSED err_t task_mmap(void *addr, size_t length, int prot, int flags,
                            int fd, off_t offset, void **out);
WARN_UNUSED err_t task_fd_dup2(u64 oldfd, u64 newfd);
WARN_UNUSED err_t task_fd_pipe(u64 fd[2]);
void task_set_wait(struct vfs_fd *fd, int flag);
struct task *get_current_task(void);
void task_new_core_init(void);
err_t task_get_from_pid(u64 pid, struct task **out);
WARN_UNUSED err_t task_variable_get(struct task *task, struct sv key,
                                    struct environment_variable **out,
                                    bool assign);
WARN_UNUSED err_t task_variable_add(struct task *task, struct sv key,
                                    struct sv value);
void task_exit(u8 exit_code);
#endif // TASK_H
