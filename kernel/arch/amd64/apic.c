#include <arch/amd64/apic.h>
#include <arch/amd64/idt.h>
#include <arch/amd64/msr.h>
#include <arch/amd64/smp.h>
#include <mmu.h>
#include <stddef.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_BSP 0x100 // Processor is a BSP
#define IA32_APIC_BASE_MSR_ENABLE 0x800

volatile void *apic_physical_base;
volatile void *apic_virtual_base;

bool apic_check(void) {
  struct cpuid_values values;
  cpuid(1, &values);
  return values.edx & CPUID_FEAT_EDX_APIC;
}

volatile void *apic_get_base(void) {
  uint64_t value;
  value = msr_get(IA32_APIC_BASE_MSR);

  u64 mask = 0xfffff000;
#ifdef __PHYSICAL_MEMORY_EXTENSION__
  mask |= 0x0f << 32;
#endif

  apic_physical_base = (void *)(value & mask);
  return apic_physical_base;
}

void apic_set_base(volatile void *apic) {
  apic_virtual_base = NULL;
  u64 value = ((uintptr_t)apic & 0xfffff0000) | IA32_APIC_BASE_MSR_ENABLE;

#ifdef __PHYSICAL_MEMORY_EXTENSION__
  value |= ((uintptr_t)apic >> 32) & 0x0f;
#endif

  msr_set(IA32_APIC_BASE_MSR, value);
  apic_physical_base = (void *)apic;
}

void apic_register_write(u16 reg, u32 value) {
  volatile u32 *ptr = (u32 *)((u8 *)apic_virtual_base + reg);
  *ptr = value;
}

u32 apic_register_read(u16 reg) {
  volatile u32 *ptr = (u32 *)((u8 *)apic_virtual_base + reg);
  return *ptr;
}

bool apic_map_base(void) {
  // TODO: error
  apic_virtual_base =
      mmu_map_frames((void *)apic_physical_base, 0x400,
                     MMU_FLAG_PCD | MMU_FLAG_RW | MMU_FLAG_PRESENT);
  return (NULL != apic_virtual_base);
}

#define APIC_REGISTER_TIMER_INITCNT 0x380

#define APIC_REGISTER_LVT_TIMER 0x320
#define APIC_REGISTER_TIMER_DIV 0x3E0
#define APIC_REGISTER_TIMER_CURRCNT 0x390

#define APIC_REGISTER_EOI 0xB0

#define APIC_LVT_TIMER_MODE_PERIODIC 0x20000

static void apic_start_timer() {
  apic_register_write(APIC_REGISTER_TIMER_DIV, 0x3);

  // TODO: Use the correct number
  uint32_t ticksIn10ms = 0x10000;

  // Start timer as periodic on IRQ 0, divider 16, with the number of ticks we
  // counted
  apic_register_write(APIC_REGISTER_LVT_TIMER,
                      32 | APIC_LVT_TIMER_MODE_PERIODIC);
  apic_register_write(APIC_REGISTER_TIMER_DIV, 0x3);
  apic_register_write(APIC_REGISTER_TIMER_INITCNT, ticksIn10ms);
}

void int_apic_clock(struct cpu_status *r) {
  apic_register_write(APIC_REGISTER_EOI, 0);
  bool is_kernel = (0x08 == r->iret_cs);
  if (is_kernel) {
    return;
  }
  task_legacy_switch();
}

void apic_timer_install(void) {
  set_handler(0x20, (interrupt_handler)int_apic_clock);
  apic_start_timer();
}

bool apic_enable(void) {
  if (!apic_check()) {
    return false;
  }
  apic_set_base(apic_get_base());

  apic_map_base();

  apic_register_write(0xF0, apic_register_read(0xF0) | (1 << 8));
  return true;
}
