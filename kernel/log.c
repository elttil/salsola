#include <drivers/serial.h>
#include <kprintf.h>
#include <log.h>
#include <stdarg.h>

int log_to_screen = 0;

struct stackframe {
  struct stackframe *ebp;
  u32 eip;
};

u32 x = 0;
u32 y = 0;

void klog(int code, char *fmt, ...) {
  va_list list;
  va_start(list, fmt);
  switch (code) {
  case LOG_SUCCESS:
    kprintf("[SUCCESS] ");
    break;
  case LOG_NOTE:
    kprintf("[NOTE] ");
    break;
  case LOG_WARN:
    kprintf("[WARN] ");
    break;
  default:
  case LOG_ERROR:
    kprintf("[ERROR] ");
    break;
  }
  vkprintf(fmt, list);
  va_end(list);
  kprintf("\n");
}
