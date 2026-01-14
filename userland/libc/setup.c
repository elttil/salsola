#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/random.h>

FILE *__stdin_FILE;
FILE *__stdout_FILE;
FILE *__stderr_FILE;
#define stdin __stdin_FILE
#define stdout __stdout_FILE
#define stderr __stderr_FILE

int main(int argc, char **argv);

void _libc_setup(int argc, char **argv) {
  sarandom_init(NULL);
  sarandom_set_aggressive_key_overwriting(NULL);

  __stdin_FILE = fdopen(0, "r");
  __stdout_FILE = fdopen(1, "w");
  __stderr_FILE = fdopen(2, "w");

  exit(main(argc, argv));
  assert(0);
}
