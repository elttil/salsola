#include <arch/amd64/msr.h>
#include <arch/amd64/smap.h>
#include <stdbool.h>

#include <kprintf.h>

void asm_smap_enable(void);
void asm_smap_disable(void);

#define CPUID_EBX_SMAP (1 << 20)

static bool has_smap(void) {
  struct cpuid_values values;
  cpuid(7, &values);
  return (0 != (values.ebx & CPUID_EBX_SMAP));
}

void smap_set(bool flag) {
  static int state = 0;
  if (2 == state) {
    return;
  }
  if (unlikely(0 == state)) {
    if (has_smap()) {
      state = 1;
    } else {
      state = 2;
      return;
    }
  }

  if (flag) {
    asm_smap_enable();
  } else {
    asm_smap_disable();
  }
}
