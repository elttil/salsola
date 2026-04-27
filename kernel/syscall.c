#include <arch/amd64/idt.h>
#include <assert.h>
#include <csprng.h>
#include <error.h>
#include <mmu.h>
#include <stddef.h>
#include <sv.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>
#include <syscalls.h>
#include <task.h>

#include <kprintf.h>

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
    UNUSED(task_fd_close(tmp_fd));
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

err_t syscall_pwrite(u64 fd, const void *buffer, u64 count, size_t offset,
                     u64 *out) {
  TRY(mmu_verify_user_pointer(buffer, count));
  TRY(mmu_verify_user_pointer(out, sizeof(u64)));
  return task_fd_pwrite(fd, buffer, count, offset, out);
}

err_t syscall_pread(u64 fd, void *buffer, u64 count, size_t offset, u64 *out) {
  TRY(mmu_verify_user_pointer(buffer, count));
  TRY(mmu_verify_user_pointer(out, sizeof(u64)));
  return task_fd_pread(fd, buffer, count, offset, out);
}

err_t syscall_randomfill(void *buffer, uint32_t size) {
  // TODO: Crash the process upon error.
  TRY(mmu_verify_user_pointer(buffer, size));
  csprng_get_random(buffer, size);
  return ERROR_SUCCESS;
}

err_t syscall_getdent(u64 fd, struct vfs_dirent *dirp, size_t dir_entry_size,
                      u64 nentries, u64 *rc) {
  // TODO: Avoid multiplication overflow
  TRY(mmu_verify_user_pointer(dirp, dir_entry_size * nentries));
  TRY(mmu_verify_user_pointer(rc, sizeof(*rc)));

  return task_fd_getdent(fd, dirp, dir_entry_size, nentries, rc);
}

err_t syscall_exec(const char *str, u32 length, char *args[], u32 arg_lengths[],
                   u32 num_of_args, char *envs[], u32 *env_lengths,
                   u32 num_of_envs) {
  TRY(mmu_verify_user_pointer(arg_lengths, sizeof(u32) * num_of_args));
  TRY(mmu_verify_user_pointer(env_lengths, sizeof(u32) * num_of_envs));
  // TODO: Clone argv
  struct sv f;
  TRY(mmu_get_user_sv(str, length, &f));
  struct sv file = sv_clone(f);

  struct sv *new_args = kallocarray(num_of_args, sizeof(struct sv));
  if (!new_args) {
    return ERROR_NO_MEMORY;
  }

  struct sv *new_envs = kallocarray(num_of_envs, sizeof(struct sv));
  if (!new_envs) {
    kfree(new_args);
    return ERROR_NO_MEMORY;
  }

  for (u32 i = 0; i < num_of_args; i++) {
    struct sv tmp;
    TRY(mmu_get_user_sv(args[i], arg_lengths[i], &tmp));
    new_args[i] = sv_clone(tmp);
  }

  for (u32 i = 0; i < num_of_envs; i++) {
    struct sv tmp;
    TRY(mmu_get_user_sv(envs[i], env_lengths[i], &tmp));
    new_envs[i] = sv_clone(tmp);
  }

  return task_exec(file, new_args, num_of_args, new_envs, num_of_envs);
}

err_t syscall_mmap(void *addr, size_t length, int prot, int flags, int fd,
                   off_t offset, void **out) {
  // NOTE: task_mmap has to ensure that addr can not be used in a
  // malicious way to allocate within the kernel region.
  TRY(mmu_verify_user_pointer(out, sizeof(void *)));
  return task_mmap(addr, length, prot, flags, fd, offset, out);
}

err_t syscall_fstat(u64 fd, struct stat *buf) {
  return task_fd_fstat(fd, buf);
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

err_t syscall_pipe(u64 fds[2]) {
  // TODO: Maybe add a mmu_verified_memcpy function.
  TRY(mmu_verify_user_pointer(fds, sizeof(u64[2])));
  return task_fd_pipe(fds);
}

err_t syscall_kpoll(u64 fd, struct kevent *events, size_t nevents,
                    size_t *nchanges, i32 timeout) {
  // TODO: DOES NOT CORRECTLY CHECK POINTERS.
  return kpoll(fd, events, nevents, nchanges, timeout);
}

void syscall_exit(u8 exit_code) {
  task_exit(exit_code);
  assert(0);
}

err_t syscall_waitfd(int fd, u8 *error_code, pid_t *_pid) {
  u8 rc;
  pid_t pid;
  TRY(mmu_verify_user_pointer(error_code, sizeof(u8)));
  TRY(mmu_verify_user_pointer(_pid, sizeof(pid_t)));
  err_t err = task_waitfd(fd, &rc, &pid);
  if (ERROR_SUCCESS != err) {
    return err;
  }
  TRY(mmu_verify_user_pointer(_pid, sizeof(pid_t)));
  if (error_code) {
    *error_code = rc;
  }
  if (_pid) {
    *_pid = pid;
  }
  return err;
}

err_t syscall_getcwd(char *buffer, size_t size) {
  TRY(mmu_verify_user_pointer(buffer, size));
  return task_getcwd(buffer, size);
}

err_t syscall_chdir(char *_path, size_t length) {
  struct sv path;
  TRY(mmu_get_user_sv(_path, length, &path));
  return task_chdir(path);
}

err_t syscall_fcntl(int fd, int cmd, int arg) {
  return task_fcntl(fd, cmd, arg);
}

err_t syscall_ftruncate(u64 fd, u64 length) {
  return task_fd_truncate(fd, length);
}

err_t syscall_namespace_override(char *_path, size_t length, u64 fd) {
  struct sv path;
  TRY(mmu_get_user_sv(_path, length, &path));
  return task_add_namespace_override(path, fd);
}

void syscall_msleep(u64 ms) {
  return task_msleep(ms);
}

void *syscall_get_physical(void *address) {
  // TODO: Proper checks
  if ((uintptr_t)address >= 0xF000000000) {
    kprintf("too big\n");
    return NULL;
  }
  bool exists;
  void *rc = mmu_virtual_to_physical(address, &exists);
  if (!exists) {
    kprintf("does not exist??\n");
    return NULL;
  }
  return rc;
}

err_t syscall_recvfd(fd_t fd, fd_t *out_fd) {
  return task_fd_recvfd(fd, out_fd);
}

err_t syscall_sendfd(fd_t fd, fd_t inc_fd) {
  return task_fd_sendfd(fd, inc_fd);
}

u64 syscall_handler(const struct syscall_arguments *regs) {
  u64 syscall = regs->rdi;
  const u64 args[] = {regs->rsi, regs->rdx, regs->rbx, regs->r8,
                      regs->r9,  regs->r10, regs->r12, regs->r13};
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
                        (void *)args[3], args[4], (void *)args[5],
                        (void *)args[6], args[7]);
  case SYS_DUP2:
    return syscall_dup2(args[0], args[1]);
  case SYS_PIPE:
    return syscall_pipe((void *)args[0]);
  case SYS_KPOLL:
    return syscall_kpoll(args[0], (void *)args[1], args[2], (void *)args[3],
                         args[4]);
  case SYS_WAITFD:
    return syscall_waitfd(args[0], (void *)args[1], (void *)args[2]);
  case SYS_GETCWD:
    return syscall_getcwd((void *)args[0], args[1]);
  case SYS_CHDIR:
    return syscall_chdir((void *)args[0], args[1]);
  case SYS_EXIT:
    syscall_exit(args[0]);
    break;
  case SYS_FCNTL:
    return syscall_fcntl(args[0], args[1], args[2]);
    break;
  case SYS_GETDENT:
    return syscall_getdent(args[0], (void *)args[1], args[2], args[3],
                           (void *)args[4]);
    break;
  case SYS_FTRUNCATE:
    return syscall_ftruncate(args[0], args[1]);
    break;
  case SYS_NAMESPACE_OVERRIDE:
    return syscall_namespace_override((void *)args[0], args[1], args[2]);
    break;
  case SYS_MSLEEP:
    syscall_msleep(args[0]);
    break;
  case SYS_GET_PHYSICAL:
    return (u64)syscall_get_physical((void *)args[0]);
    break;
  case SYS_PREAD:
    return syscall_pread(args[0], (void *)args[1], args[2], args[3],
                         (u64 *)args[4]);
  case SYS_PWRITE:
    return syscall_pwrite(args[0], (void *)args[1], args[2], args[3],
                          (u64 *)args[4]);
  case SYS_RECVFD:
    return syscall_recvfd(args[0], (void *)args[1]);
  case SYS_SENDFD:
    return syscall_sendfd(args[0], args[1]);
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
