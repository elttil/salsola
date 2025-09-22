#include <assert.h>
#include <csprng.h>
#include <drivers/ps2_keyboard.h>
#include <drivers/serial.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <mmu.h>
#include <prng.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/amd64/gdt.h>
#include <arch/amd64/idt.h>

#include "multiboot2.h"

void kmain(unsigned long magic, void *arg) {
  gdt_init();

  csprng_init();
  prng_init();

  serial_init();
  if (MULTIBOOT2_BOOTLOADER_MAGIC != magic) {
    kprintf("Invalid magic: %x\n", magic);
    return;
  }

  assert(mmu_init(arg));
  assert(kmalloc_init());

  idt_init();

  assert(ps2_keyboard_init());

  kprintf("Hello, world!\n");
  return;
}
