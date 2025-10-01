#include <arch/amd64/apic.h>
#include <arch/amd64/msr.h>
#include <mmu.h>
#include <stddef.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_BSP 0x100 // Processor is a BSP
#define IA32_APIC_BASE_MSR_ENABLE 0x800

void *apic_physical_base;
void *apic_virtual_base;

bool apic_check(void) {
  struct cpuid_values values;
  cpuid(1, &values);
  return values.edx & CPUID_FEAT_EDX_APIC;
}

void *apic_get_base(void) {
  uint64_t value;
  value = msr_get(IA32_APIC_BASE_MSR);

  u64 mask = 0xfffff000;
#ifdef __PHYSICAL_MEMORY_EXTENSION__
  mask |= 0x0f << 32;
#endif

  apic_physical_base = (void *)(value & mask);
  return apic_physical_base;
}

void apic_set_base(void *apic) {
  apic_virtual_base = NULL;
  u64 value = ((uintptr_t)apic & 0xfffff0000) | IA32_APIC_BASE_MSR_ENABLE;

#ifdef __PHYSICAL_MEMORY_EXTENSION__
  value |= ((uintptr_t)apic >> 32) & 0x0f;
#endif

  msr_set(IA32_APIC_BASE_MSR, value);
  apic_physical_base = (void *)apic;
}

void apic_write_register(u16 reg, u32 value) {
  u32 *ptr = (u32 *)((u8 *)apic_virtual_base + reg);
  *ptr = value;
}

u32 apic_read_register(u16 reg) {
  u32 *ptr = (u32 *)((u8 *)apic_virtual_base + reg);
  return *ptr;
}

bool apic_map_base(void) {
  // TODO: error
  apic_virtual_base = mmu_map_frames(apic_physical_base, 0x400);
  return (NULL != apic_virtual_base);
}

bool apic_enable(void) {
  if (!apic_check()) {
    return false;
  }
  apic_set_base(apic_get_base());

  apic_map_base();

  apic_write_register(0xF0, apic_read_register(0xF0) | 0x100);
  return true;
}
