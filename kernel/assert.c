#include <arch/amd64/idt.h>
#include <assert.h>
#include <kprintf.h>
#include <log.h>

void dump_backtrace(u32 max_frames);
void aFailed(char *f, int l, char *expr) {
  interrupts_disable();
  klog(LOG_ERROR, "Assert failed: %s\n", expr);
  kprintf("%s : %d\n", f, l);
  dump_backtrace(18);
  for (;;)
    ;
}
