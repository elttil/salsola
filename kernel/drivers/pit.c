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

  outb(PIT_IO_MODE_COMMAND, 0b0000010);

  count = inb(PIT_IO_CHANNEL_0);
  count |= ((u16)inb(PIT_IO_CHANNEL_0)) << 8;

  return count;
}

void pit_sleep(u32 rounds) {
  pit_set_count(0x00FF);
  u16 last_value = pit_read_count();
  u32 i = 0;
  for (; i < rounds;) {
    u16 new_value = pit_read_count();
    if (new_value > last_value) {
      i++;
    }
    last_value = new_value;
  }
}

void pit_set_count(u16 count) {
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
  u8 mode = 0b001;
  (void)mode;
  outb(PIT_IO_MODE_COMMAND,
       0b00110000 | (mode << 1) /*0b00110000 | (mode << 1)*/);
  outb(PIT_IO_CHANNEL_0, count & 0xFF);
  outb(PIT_IO_CHANNEL_0, (count & 0xFF00) >> 8);
}

void int_clock(struct cpu_status *r) {
  (void)r;
  eoi(0x20);
}

void pit_install(void) {
  handler_install(0x20, int_clock, 0);
}
