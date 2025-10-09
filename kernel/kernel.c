#include <assert.h>
#include <csprng.h>
#include <drivers/ahci.h>
#include <drivers/pit.h>
#include <drivers/ps2_keyboard.h>
#include <drivers/serial.h>
#include <fs/ext2.h>
#include <fs/ramfs.h>
#include <fs/vfs.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <mmu.h>
#include <prng.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <task.h>

#include <arch/amd64/apic.h>
#include <arch/amd64/gdt.h>
#include <arch/amd64/idt.h>
#include <arch/amd64/msr.h>
#include <arch/amd64/smp.h>

#include "multiboot2.h"

struct multiboot_tag *tags;

void kmain2(void) {
  assert(kmalloc_init());

  assert(msr_is_available());

  idt_init();
  assert(apic_enable());

  assert(ps2_keyboard_init());

  // smp_init(tags);
  mmu_remove_identity();

  assert(task_init());

  pit_install();
  pit_set_count(2);

  vfs_add_mount(C_TO_SV("/dev"), ramfs_create());

  ahci_init();

  struct vfs_fd *sda_fd = vfs_open(C_TO_SV("/dev/sda"), 0, NULL);

  vfs_add_mount(C_TO_SV("/"), ext2_create(sda_fd));

  __asm__("sti");

  int pid = task_fork(NULL);
  if (0 == pid) {
    task_exec(C_TO_SV("/init"));
    assert(0);
  }
  task_legacy_switch();
  for (;;)
    ;
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

  assert(mmu_init(arg));

  tags = (void *)((uintptr_t)arg + 0xFFFFFF8000000000 + 8);
  mmu_update_stack(kmain2);
  // Don't do anything after this.
}
