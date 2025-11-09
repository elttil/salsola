#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int execv(const char *pathname, char *const argv[]) {
  struct sv f = C_TO_SV(pathname);

  int argc = 0;
  for (; argv[argc]; argc++)
    ;

  struct sv *args = allocarray(sizeof(struct sv), argc);
  for (int i = 0; i < argc; i++) {
    args[i] = C_TO_SV(argv[i]);
  }

  errno = error_to_errno(sa_exec(f, args, argc));
  return -1;
}
