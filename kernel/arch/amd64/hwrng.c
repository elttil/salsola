#include <arch/amd64/msr.h>
#include <io.h>
#include <hwrng.h>

#define CPUID_RDRAND (1 << 30)

bool rdrand_is_avaiable = false;

static bool has_rdrand(void) {
  struct cpuid_values values;
  cpuid(1, &values);
  return (0 != (values.ecx & CPUID_RDRAND));
}

bool hwrng_init(void) {
  return (rdrand_is_avaiable = has_rdrand());
}

bool hwrng_get(u64 *output) {
  if (!output) {
    return false;
  }
  if (rdrand_is_avaiable) {
    return rdrand(output);
  }
  return false;
}
