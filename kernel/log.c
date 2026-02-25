#include <drivers/framebuffer.h>
#include <drivers/serial.h>
#include <kprintf.h>
#include <log.h>
#include <stdarg.h>

struct stackframe {
  struct stackframe *ebp;
  u32 eip;
};

static u32 x = 0;
static u32 y = 0;

static bool log_to_screen = false;

#define RED 0xFF0000
#define BLACK 0x000000

void log_enable_screen(void) {
  if(log_to_screen) return;
  x = 0;
  y = 0;
  log_to_screen = true;
  framebuffer_clear_screen(RED);
  klog(LOG_NOTE, "Logging Enabled");
}

void log_disable_screen(void) {
  if(!log_to_screen) return;
  log_to_screen = false;
  framebuffer_clear_screen(BLACK);
}

void log_print_char(char c) {
  serial_print_char(c);
  if (log_to_screen) {
    if ('\n' == c) {
      x = 0;
      y += 8;
    }
    framebuffer_drawfont(x, y, c);
    x += 8;
  }
}

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
