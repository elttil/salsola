// TODO: Clean this up. Right now I just want it to work.
#include <arch/amd64/gdt.h>
#include <stdint.h>

struct GDTR {
  uint16_t limit;
  uint64_t address;
} __attribute__((packed));

uint64_t num_gdt_entries = 3;
uint64_t gdt_entries[3];

void load_gdt(void *gdtr);
void gdt_init(void) {
  gdt_entries[0] = 0;

  uint64_t kernel_code = 0;
  kernel_code |= 0b0011 << 8;
  kernel_code |= 1 << 11;
  kernel_code |= 1 << 12;
  kernel_code |= 0 << 13;
  kernel_code |= 1 << 15;
  kernel_code |= 1 << 21;
  gdt_entries[1] = kernel_code << 32;

  uint64_t kernel_data = 0;
  kernel_data |= 0b0011 << 8;
  kernel_data |= 1 << 12;
  kernel_data |= 0 << 13;
  kernel_data |= 1 << 15;
  kernel_data |= 1 << 21;
  gdt_entries[2] = kernel_data << 32;

  struct GDTR example_gdtr = {.limit = num_gdt_entries * sizeof(uint64_t) - 1,
                              .address = (uint64_t)gdt_entries};

  load_gdt(&example_gdtr);
}
