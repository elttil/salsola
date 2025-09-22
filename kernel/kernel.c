#include <assert.h>
#include <csprng.h>
#include <drivers/ahci.h>
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

void kmain(u32 magic, void *arg) {
  csprng_init();
  prng_init();

  serial_init();

  gdt_init();
  idt_init();

  if (MULTIBOOT2_BOOTLOADER_MAGIC != magic) {
    kprintf("Invalid magic: %x\n", magic);
    return;
  }

  assert(mmu_init(arg));
}

void kmain2(void) {
  assert(kmalloc_init());

  assert(ps2_keyboard_init());

  ahci_init();
  kprintf("Hello, world!\n");
  for (;;)
    ;
  return;
}
