#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>

#include <fcntl.h>
#include <unistd.h>

#include <syscall.h>

#include <stdio.h>

FILE *__stdin_FILE;
FILE *__stdout_FILE;
FILE *__stderr_FILE;
#define stdin __stdin_FILE
#define stdout __stdout_FILE
#define stderr __stderr_FILE

int main(int argc, char **argv, char **envp);

void _libc_setup(int argc, char **argv, char **envp) {
  sarandom_init(NULL);
  sarandom_set_aggressive_key_overwriting(NULL);
  // TODO: Maybe do something different.
  // Can we use sarandom without it being too expensive?
  srand((envp) ? ((unsigned int)((uintptr_t)*envp & 0xFFFF)) : 0xBAADBEEF);

  __stdin_FILE = fdopen(0, "r");
  __stdout_FILE = fdopen(1, "w");
  __stderr_FILE = fdopen(2, "w");
  // HACK
  if (!__stderr_FILE) {
    __stderr_FILE = __stdout_FILE;
  }
  char **e = envp;
  for (; *e; e++) {
    char *d = strchr(*e, '=');
    char *n = *e;
    if ('\0' == *d) {
      // This shouldn't happen, but we can just do this
      // to avoid a crash.
      setenv(n, "", 1);
      continue;
    }
    char *v = d + 1;
    *d = '\0';
    setenv(n, v, 1);
    *d = '=';
  }

  exit(main(argc, argv, envp));
  assert(0);
}
