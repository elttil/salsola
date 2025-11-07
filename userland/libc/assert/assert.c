#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int debug_printf(const char *fmt, ...);

void aFailed(char *f, int l) {
  printf("Assert failed\n");
  printf("%s : %d\n", f, l);
  // TODO: Exit
  for (;;)
    ;
  //  exit(1);
}
