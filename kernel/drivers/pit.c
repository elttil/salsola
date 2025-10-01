#include <arch/amd64/idt.h>
#include <drivers/pit.h>
#include <io.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <task.h>

#define PIT_IO_CHANNEL_0 0x40
#define PIT_IO_MODE_COMMAND 0x43

u32 pit_counter = 0;
u32 switch_counter = 0;
u16 hertz;

u16 pit_read_count(void) {
  u16 count = 0;

  outb(PIT_IO_MODE_COMMAND, 0x0 /*0b00000000*/);

  count = inb(PIT_IO_CHANNEL_0);
  count |= inb(PIT_IO_CHANNEL_0) << 8;

  return count;
}

void pit_set_count(u16 _hertz) {
  hertz = _hertz;
  u16 divisor = 1193180 / hertz;

  /*
   * 0b00110110
   *   ^^
   * channel - 0
   *     ^^
   * r/w mode - LSB then MSB
   *       ^^^
   * mode - 3 Square Wave Mode
   *          ^
   * BCD - no
   */
  outb(PIT_IO_MODE_COMMAND, 0x36 /*0b00110110*/);
  outb(PIT_IO_CHANNEL_0, divisor & 0xFF);
  outb(PIT_IO_CHANNEL_0, (divisor & 0xFF00) >> 8);
}

void int_clock(struct cpu_status *r) {
  (void)r;
  eoi(0x20);
  task_legacy_switch();
}

void pit_install(void) {
  handler_install(0x20, int_clock);
}
