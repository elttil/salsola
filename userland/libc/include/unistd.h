#ifndef UNISTD_H
#define UNISTD_H
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <tb/sv.h>

#define RC_ERROR_TO_ERRNO(expr)                                                \
  {                                                                            \
    errno = error_to_errno(expr);                                              \
    return (errno > 0) ? -1 : 0;                                               \
  }

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

extern int opterr, optind, optopt;
extern char *optarg;

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);

err_t sa_read(int fd, void *buf, u64 count, u64 *out);
err_t sa_write(int fd, const void *buf, u64 count, u64 *out);
err_t sa_lseek(u64 fd, off_t offset, int whence, off_t *out);

int close(int fd);

err_t sa_close(u64 fd);

err_t sa_exec(struct sv file, struct sv *args, u32 number_of_args);
int execvp(const char *file, char *const argv[]);
int execv(const char *pathname, char *const argv[]);

void *sbrk(intptr_t increment);

int pipe(int fd[2]);
err_t sa_pipe(u64 fd[2]);

pid_t getpid(void);
err_t sa_chdir(struct sv path);

int chdir(const char *path);
char *getcwd(char *buf, size_t size);
#endif
