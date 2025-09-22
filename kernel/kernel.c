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

void kmain2(void) {
  assert(kmalloc_init());

  assert(ps2_keyboard_init());

  ahci_init();
  kprintf("Hello, world!\n");
}

void kmain(u32 magic, void *arg) {
  if (MULTIBOOT2_BOOTLOADER_MAGIC != magic) {
    kprintf("Invalid magic: %x\n", magic);
    return;
  }

  csprng_init();
  prng_init();

  serial_init();

  gdt_init();
  idt_init();

  assert(mmu_init(arg));
  mmu_update_stack(kmain2);
  // Don't do anything after this.
}
