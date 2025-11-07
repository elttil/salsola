#include <arch/amd64/idt.h>
#include <assert.h>
#include <csprng.h>
#include <error.h>
#include <kprintf.h>
#include <mmu.h>
#include <stddef.h>
#include <sv.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>
#include <syscalls.h>
#include <task.h>

struct syscall_arguments {
  uint64_t rbx;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t rbp;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rsp;
} __attribute__((packed));

err_t syscall_open(u64 *user_fd, char *str, size_t length, int flags) {
  struct sv file;
  TRY(mmu_get_user_sv(str, length, &file));

  u64 tmp_fd;
  TRY(task_fd_open(&tmp_fd, file, flags));

  err_t rc;
  if (ERROR_SUCCESS !=
      (rc = mmu_assign_user_ptr(user_fd, &tmp_fd, sizeof(int)))) {
    task_fd_close(tmp_fd);
    return rc;
  }

  return ERROR_SUCCESS;
}

err_t syscall_close(int fd) {
  return task_fd_close(fd);
}

err_t syscall_write(u64 fd, const void *buffer, u64 count, u64 *out) {
  TRY(mmu_verify_user_pointer(buffer, count));
  TRY(mmu_verify_user_pointer(out, sizeof(u64)));
  return task_fd_write(fd, buffer, count, out);
}

err_t syscall_read(u64 fd, void *buffer, u64 count, u64 *out) {
  TRY(mmu_verify_user_pointer(buffer, count));
  TRY(mmu_verify_user_pointer(out, sizeof(u64)));
  return task_fd_read(fd, buffer, count, out);
}

err_t syscall_randomfill(void *buffer, uint32_t size) {
  // TODO: Crash the process upon error.
  TRY(mmu_verify_user_pointer(buffer, size));
  csprng_get_random(buffer, size);
  return ERROR_SUCCESS;
}

err_t syscall_exec(const char *str, u32 length, char *args[], u32 arg_lengths[],
                   u32 num_of_args) {
  // TODO: Clone argv
  struct sv f;
  TRY(mmu_get_user_sv(str, length, &f));
  struct sv file = sv_clone(f);

  struct sv *new_args = kallocarray(num_of_args, sizeof(struct sv));
  if (!new_args) {
    return ERROR_NO_MEMORY;
  }

  for (u64 i = 0; i < num_of_args; i++) {
    struct sv tmp;
    TRY(mmu_get_user_sv(args[i], arg_lengths[i], &tmp));
    new_args[i] = sv_clone(tmp);
  }

  task_exec(file, new_args, num_of_args);
  return ERROR_SUCCESS;
}

err_t syscall_mmap(void *addr, size_t length, int prot, int flags, int fd,
                   off_t offset, void **out) {
  // NOTE: task_mmap has to ensure that addr can not be used in a
  // malicious way to allocate within the kernel region.
  TRY(mmu_verify_user_pointer(out, sizeof(void *)));
  return task_mmap(addr, length, prot, flags, fd, offset, out);
}

err_t syscall_fstat(u64 fd, struct stat *buf) {
  // TODO:
  (void)fd;
  (void)buf;
  return ERROR_SUCCESS;
}

err_t syscall_fork(pid_t *pid) {
  TRY(mmu_verify_user_pointer(pid, sizeof(pid_t)));
  u64 p;
  err_t err = task_fork(&p);
  if (pid) {
    *pid = p;
  }
  return err;
}

err_t syscall_dup2(u64 oldfd, u64 newfd) {
  return task_fd_dup2(oldfd, newfd);
}

u64 syscall_handler(const struct syscall_arguments *regs) {
  u64 syscall = regs->rdi;
  const u64 args[7] = {regs->rsi, regs->rdx, regs->rbx, regs->r8,
                       regs->r9,  regs->r10, regs->r12};
  switch (syscall) {
  case SYS_OPEN:
    return syscall_open((u64 *)args[0], (char *)args[1], (size_t)args[2],
                        (int)args[3]);
  case SYS_CLOSE:
    return syscall_close(args[0]);
  case SYS_READ:
    return syscall_read(args[0], (void *)args[1], args[2], (u64 *)args[3]);
  case SYS_WRITE:
    return syscall_write(args[0], (const void *)args[1], args[2],
                         (u64 *)args[3]);
  case SYS_MMAP:
    return syscall_mmap((void *)args[0], args[1], args[2], args[3], args[4],
                        args[5], (void **)args[6]);
  case SYS_RANDOMFILL:
    return syscall_randomfill((void *)args[0], args[1]);
  case SYS_LSEEK:
    return task_lseek(args[0], args[1], args[2], (off_t *)args[3]);
  case SYS_FSTAT:
    return syscall_fstat(args[0], (void *)args[1]);
  case SYS_FORK:
    return syscall_fork((void *)args[0]);
  case SYS_EXEC:
    return syscall_exec((void *)args[0], args[1], (void *)args[2],
                        (void *)args[3], args[4]);
  case SYS_DUP2:
    return syscall_dup2(args[0], args[1]);
  default:
    assert(0);
    break;
  };
  return 0;
}

void setup_syscall(void);
u64 set_kernel_stack(void *stack);

void syscall_init(void) {
  set_kernel_stack((void *)0xffffff8000000000 - 0x1000 /*Guard page*/);
  setup_syscall();
}
