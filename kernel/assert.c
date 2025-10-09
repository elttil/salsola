#include <assert.h>
#include <kprintf.h>

__attribute__((__noreturn__)) void aFailed(char *f, int l) {
  __asm__("cli");
  kprintf("Assert failed\n");
  kprintf("%s : %d\n", f, l);
  for (;;)
    ;
  //  dump_backtrace(10);
  //  halt();
  //  kprintf("after halt?\n");
}
