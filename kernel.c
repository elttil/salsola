#include <assert.h>
#include <drivers/serial.h>
#include <kprintf.h>
#include <mmu.h>
#include <stddef.h>
#include <stdint.h>

#include "multiboot2.h"

void kmain(unsigned long magic, void *arg) {
  serial_init();
  if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
    kprintf("Invalid magic: %x\n", magic);
    return;
  }

  assert(mmu_init(arg));

  kprintf("Hello, world!\n");

  uintptr_t addr = (uintptr_t)arg + 0xFFFFFF8000000000;
  for (struct multiboot_tag *tag = (struct multiboot_tag *)(addr + 8);
       tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = (struct multiboot_tag *)((multiboot_uint8_t *)tag +
                                      ((tag->size + 7) & ~7))) {
    kprintf("tag: %d, size: %d\n", tag->type, tag->size);
  }
  return;
}
