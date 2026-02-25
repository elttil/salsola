#include <assert.h>
#include <csprng.h>
#include <drivers/ahci.h>
#include <drivers/framebuffer.h>
#include <drivers/pit.h>
#include <drivers/ps2_keyboard.h>
#include <drivers/serial.h>
#include <fcntl.h>
#include <fs/ext2.h>
#include <fs/procfs.h>
#include <fs/ramdisk.h>
#include <fs/ramfs.h>
#include <fs/vfs.h>
#include <hwrng.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <log.h>
#include <mmu.h>
#include <prng.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/kpoll.h>
#include <syscall.h>
#include <task.h>
#include <timer.h>

#include <arch/amd64/apic.h>
#include <arch/amd64/gdt.h>
#include <arch/amd64/idt.h>
#include <arch/amd64/msr.h>
#include <arch/amd64/smp.h>

#include <crypto/SHA1/sha1.h>

#include "multiboot2.h"

void debug_hash_file(struct sv file) {
  kprintf("Hashing: " SV_FMT "\n", SV_FMT_ARG(file));
  SHA1_CTX ctx;
  SHA1_Init(&ctx);
  struct vfs_fd *fd = vfs_open(file, 0, NULL);
  for (;;) {
    char buffer[1001];
    size_t rc = vfs_read(fd, buffer, 1001, NULL);
    if (0 == rc) {
      break;
    }
    SHA1_Update(&ctx, buffer, rc);
  }
  unsigned char digest[SHA1_LEN];
  SHA1_Final(&ctx, digest);

  kprintf("Hash: ");
  for (int i = 0; i < SHA1_LEN; i++) {
    kprintf("%02x", digest[i]);
  }
  kprintf("\n");
}

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

bool path_open(struct sb *out, struct sv directory, struct sv path);

struct multiboot_tag *tags;
u64 bspid_get();
void kmain2(void) {
  assert(kmalloc_init());

  for (struct multiboot_tag *tag = tags; tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = (struct multiboot_tag *)((multiboot_uint8_t *)tag +
                                      ((tag->size + 7) & ~7))) {
    if (MULTIBOOT_TAG_TYPE_FRAMEBUFFER == tag->type) {
      display_driver_init((struct multiboot_tag_framebuffer_common *)tag);
    }
  }
  log_enable_screen();
  klog(LOG_NOTE, "Log start");

  klog(LOG_NOTE, "Trying to initialize time");
  timer_init();

  klog(LOG_SUCCESS, "Timer Initialized");

  assert(msr_is_available());

  klog(LOG_SUCCESS, "MSR Check complete");

  idt_init();

  klog(LOG_SUCCESS, "IDT Initialized");

  assert(apic_enable());

  klog(LOG_SUCCESS, "Enabled APIC");

  struct vfs_fd *root_disk = NULL;
  for (struct multiboot_tag *tag = tags; tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = (struct multiboot_tag *)((multiboot_uint8_t *)tag +
                                      ((tag->size + 7) & ~7))) {
    if (MULTIBOOT_TAG_TYPE_MODULE == tag->type) {
      struct multiboot_tag_module *module = (struct multiboot_tag_module *)tag;
      size_t length = module->mod_end - module->mod_start;
      void *address = mmu_map_frames((void *)module->mod_start, length,
                                     MMU_FLAG_RW | MMU_FLAG_PRESENT);
      root_disk = ramdisk_create(address, length);
      klog(LOG_NOTE, "Ramdisk found");
    }
  }

  interrupts_disable();
  smp_init(tags);
  klog(LOG_SUCCESS, "SMP Initialized");

  mmu_remove_identity();
  klog(LOG_SUCCESS, "MMU Identity mapping removed");

  assert(task_init());
  klog(LOG_SUCCESS, "Scheduler Initialized");

  assert(vfs_add_mount(C_TO_SV("/dev"), ramfs_create()));
  klog(LOG_SUCCESS, "/dev/ Created");

  ahci_init();
  klog(LOG_SUCCESS, "AHCI Initialized");
  serial_add_file();
  klog(LOG_SUCCESS, "/dev/serial created");

  if (!root_disk) {
    root_disk = vfs_open(C_TO_SV("/dev/sda"), 0, NULL);
    assert(root_disk);
    klog(LOG_SUCCESS, "/dev/sda Opened");
  }

  assert(vfs_add_mount(C_TO_SV("/"), ext2_create(root_disk)));
  assert(vfs_add_mount(C_TO_SV("/proc/"), procfs_create()));

  assert(csprng_add_random_device(C_TO_SV("/dev/random")));
  assert(csprng_add_random_device(C_TO_SV("/dev/urandom")));
  timer_add_device(C_TO_SV("/dev/clock"));

  framebuffer_add_device(C_TO_SV("/dev/window"));

  kpoll_add_device();

  assert(ps2_keyboard_init());

  setup_gs();
  syscall_init();

  apic_timer_install();

  kprintf("Kernel star time until /init: %dms\n", timer_get_ms());
  u64 pid;
  assert(ERROR_SUCCESS == task_fork(&pid));
  if (0 == pid) {
    struct sv args[1];
    args[0] = C_TO_SV("/bin/init");
    UNUSED(task_exec(C_TO_SV("/bin/init"), args, 1));
    kprintf("After exec\n");
    for (;;)
      ;
    // task_exec(C_TO_SV("/bin/font"));
    assert(0);
  }
  for (;;) {
    __asm__("sti");
    __asm__("hlt");
    __asm__("cli");
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
