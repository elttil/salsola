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
#include <syscall.h>
#include <task.h>

#include <arch/amd64/apic.h>
#include <arch/amd64/gdt.h>
#include <arch/amd64/idt.h>
#include <arch/amd64/msr.h>
#include <arch/amd64/smp.h>
#include <hwrng.h>

#include "multiboot2.h"

void swapgs(void);
// TODO: Move to different file.
void setup_gs(void) {
  u32 msr_gs_base = 0xC0000101;
  //  u32 msr_gs_kernel_base = 0xC0000102;

  void *gs_base = kmalloc(0x100);
  void *gs_kernel_base = kmalloc(0x100);
  assert(gs_base && gs_kernel_base);

  msr_set(msr_gs_base, (u64)gs_kernel_base);

  swapgs();
  msr_set(msr_gs_base, (u64)gs_base);
  swapgs();
}

struct multiboot_tag *tags;
u64 bspid_get();
void kmain2(void) {
  assert(kmalloc_init());

  assert(msr_is_available());

  idt_init();
  //  assert(apic_enable());

  assert(ps2_keyboard_init());

  // smp_init(tags);
  mmu_remove_identity();

  assert(task_init());

  vfs_add_mount(C_TO_SV("/dev"), ramfs_create());

  ahci_init();
  serial_add_file();

  struct vfs_fd *sda_fd = vfs_open(C_TO_SV("/dev/sda"), 0, NULL);

  vfs_add_mount(C_TO_SV("/"), ext2_create(sda_fd));

  csprng_add_random_device(C_TO_SV("/dev/random"));
  csprng_add_random_device(C_TO_SV("/dev/urandom"));

  setup_gs();
  syscall_init();

  pit_install();
  pit_set_count(2);

  u64 pid;
  assert(ERROR_SUCCESS == task_fork(&pid));
  if (0 == pid) {
    task_exec(C_TO_SV("/bin/init"));
    assert(0);
  }
  for (;;) {
    task_legacy_switch();
  }
}

void kmain(u32 magic, void *arg, bool has_sse) {
  if (MULTIBOOT2_BOOTLOADER_MAGIC != magic) {
    kprintf("Invalid magic: %x\n", magic);
    return;
  }

  if (!has_sse) {
    kprintf("CPU does not support SSE. The OS does not support no SSE so we "
            "don't boot :(\n");
    return;
  }

  if (!hwrng_init()) {
    klog(LOG_WARN, "HWRNG Failed to initalize");
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
