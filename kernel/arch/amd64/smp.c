#include <arch/amd64/acpi.h>
#include <arch/amd64/apic.h>
#include <arch/amd64/gdt.h>
#include <arch/amd64/idt.h>
#include <arch/amd64/msr.h>
#include <arch/amd64/regs.h>
#include <arch/amd64/smp.h>
#include <assert.h>
#include <drivers/pit.h>
#include <lock.h>
#include <mmu.h>
#include <string.h>
#include <timer.h>
#include <typedefs.h>

#include <kprintf.h>

lock_t smp_lock;
lock_t smp_lock2;
volatile size_t current_items = 0;

// FIXME: Limited to 64 cores
struct kernel_thread kernel_threads[MAX_CORES];

u8 *lapic_ptr = NULL;

void mdelay(u64 s) {
  u64 start = timer_get_ms();
  for (; timer_get_ms() - start < s;)
    ;
}

void udelay(u64 s) {
  u64 start = timer_get_us();
  for (; timer_get_us() - start < s;)
    ;
}

volatile u8 bspdone = 0;
volatile uint8_t aprunning = 0; // count how many APs have started
extern volatile u32 trampoline_cr3;

void wait_for_delivery(volatile u32 *);

u8 bspid_get(void) {
  struct cpuid_values values;
  cpuid(1, &values);
  return values.ebx >> 24;
}

void flush_tlb(void);
void enable_core(int core) {

  uint8_t bspid = bspid_get(); // BSP id and spinlock flag
                               // get the BSP's Local APIC ID
  kprintf("bspid: %d\n", bspid);
  kprintf("enbable core: %d\n", core);

  uint64_t cr3 = get_cr3();
  // uint64_t cr3;
  //__asm__ __volatile__("mov %%cr3, %%rbx" : "=b"(cr3) : :);

  volatile u32 *ptr = (u32 *)((uintptr_t)&trampoline_cr3 + 0xFFFFFF8000000000);

  *ptr = cr3;

  int i = core;
  // do not start BSP, that's already running this code
  // #if 0
  if (core == bspid) { // FIXME: Incorrect
    return;
  }
  lock_acquire(&smp_lock);
  // #endif
  //   send INIT IPI

  // FIXME: Give correct names
  volatile u32 *apic_errors = (volatile uint32_t *)(lapic_ptr + 0x280);
  volatile u32 *apic_select = (volatile uint32_t *)(lapic_ptr + 0x310);
  volatile u32 *apic_trigger = (volatile uint32_t *)(lapic_ptr + 0x300);

  *apic_errors = 0;

  *apic_select = *apic_select | (i << 24);
  *apic_trigger = (*apic_trigger & 0xfff00000) | 0x00C500;

  wait_for_delivery(apic_trigger);

  *apic_select = *apic_select | (i << 24);
  *apic_trigger = (*apic_trigger & 0xfff00000) | 0x008500;

  wait_for_delivery(apic_trigger);

  mdelay(10);
  // send STARTUP IPI (twice)
  for (int j = 0; j < 2; j++) {
    *apic_errors = 0;
    *apic_select = *apic_select | (i << 24);
    *apic_trigger = (*apic_trigger & 0xfff0f800) | 0x000608;

    udelay(200); // wait 200 usec
    wait_for_delivery(apic_trigger);
  }
  // release the AP spinlocks
  bspdone = 1;
  // now you'll have the number of running APs in 'aprunning'
}

void enable_core_asm(u64 rdi);

u8 core_id_get(void) {
  return bspid_get();
}

// extern volatile struct task *task_head;
struct task *get_task_head(void);
void setup_gs(void);
void setup_syscall(void);
u64 set_kernel_stack(void *stack);
void core_main() {
  gdt_init();
  idt_init();

  setup_gs();
  set_kernel_stack((void *)0xffffff8000000000 - 0x1000 /*Guard page*/);
  setup_syscall();
  lock_release(&smp_lock);
  mmu_remove_identity();

  __asm__("cli");

  for (; !get_task_head();)
    ;

  __asm__("cli");
  task_new_core_init();

  // TODO: Is this required?
  apic_enable();
  apic_timer_install();

  u64 i = 0;
  for (;; i++) {
    __asm__("cli");
    task_legacy_switch();
  }

  for (;;)
    ;
}

void ap_startup() {
  __asm__("cli");
  mmu_init_for_new_core(core_main);
  for (;;)
    ;
}

void smp_init(struct multiboot_tag *tags) {
  for (struct multiboot_tag *tag = tags; tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = (struct multiboot_tag *)((multiboot_uint8_t *)tag +
                                      ((tag->size + 7) & ~7))) {
    if (tag->type != MULTIBOOT_TAG_TYPE_ACPI_OLD) {
      continue;
    }

    struct multiboot_tag_old_acpi *m = (struct multiboot_tag_old_acpi *)tag;
    assert(m->size >= sizeof(struct RSDP_t));
    struct RSDP_t *rsdp = (struct RSDP_t *)m->rsdp;

    assert(rsdp_checksum((void *)rsdp, sizeof(*rsdp)));

    void *mapped_frames =
        mmu_map_frames((void *)rsdp->RsdtAddress, sizeof(struct ACPISDTHeader),
                       MMU_FLAG_PCD | MMU_FLAG_RW | MMU_FLAG_PRESENT);

    struct RSDT *header = (struct RSDT *)mapped_frames;
    // TODO: Make sure this check does not pass page boundaries(the
    // length could be corrupted)
    assert(rsdp_checksum((void *)header, header->h.Length));

    struct MADT *madt;
    assert(rsdt_find_signature(header, "APIC", (void **)&madt));
    lapic_ptr = mmu_map_frames((void *)madt->local_apic_address, 0x1000,
                               MMU_FLAG_PCD | MMU_FLAG_RW | MMU_FLAG_PRESENT);

    for (struct madt_entry *p = madt->entries;
         ((uintptr_t)p - (uintptr_t)madt) < madt->h.Length;) {
      if (0 == p->entry_type) {
        enable_core(p->local_apic.apic_processor_id);
      }

      p = (struct madt_entry *)((uintptr_t)p + p->record_length);
    }
  }
  lock_acquire(&smp_lock);
}
