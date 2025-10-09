// TODO: Clean this up. Right now I just want it to work.
#include <arch/amd64/gdt.h>
#include <kprintf.h>
#include <mmu.h>
#include <stdint.h>
#include <string.h>
#include <typedefs.h>

struct GDTR {
  uint16_t limit;
  uint64_t address;
} __attribute__((packed));

uint64_t num_gdt_entries = 3 + 2 + 1 + 1;

#define GDT_ENTRY_SIZE 0x8
#define GDT_NULL_SEGMENT 0x0
#define GDT_KERNEL_CODE_SEGMENT 0x1
#define GDT_KERNEL_DATA_SEGMENT 0x2
#define GDT_USERMODE_CODE_SEGMENT 0x3
#define GDT_USERMODE_DATA_SEGMENT 0x4
#define GDT_TSS_SEGMENT 0x5
#define GDT_TSS2_SEGMENT 0x6

struct GDT_Entry {
  u16 limit_low;
  u32 base_low : 24;
  u32 accessed : 1;
  u32 read_write : 1;             // readable for code, writable for data
  u32 conforming_expand_down : 1; // conforming for code, expand down for data
  u32 code : 1;                   // 1 for code, 0 for data
  u32 code_data_segment : 1;      // should be 1 for everything but TSS and LDT
  u32 DPL : 2;                    // privilege level
  u32 present : 1;
  u32 limit_high : 4;
  u32 available : 1; // only used in software; has no effect on hardware
  u32 long_mode : 1;
  u32 big : 1;  // 32-bit opcodes for code, u32 stack for data
  u32 gran : 1; // 1 to use 4k page addressing, 0 for byte addressing
  u8 base_high;
} __attribute__((packed));

struct tss_entry {
  uint32_t reserved_0;
  uint64_t rsp[3];
  uint64_t reserved_1;
  uint64_t ist[7];
  uint64_t reserved_2;
  uint16_t reserved_3;
  uint16_t iopb;
} __attribute__((packed));

struct tss_entry tss_entry;

typedef union {
  struct GDT_Entry s;
  u64 raw;
} GDT_Entry;
GDT_Entry gdt_entries[5 + 1 + 1];

extern void *trampoline_gdt;

struct GDTR gdtr;

void load_gdt(void *gdtr);

void gdt_change_rsp0(u64 rsp0) {
  kprintf("rsp0: %x\n", rsp0);
  tss_entry.rsp[0] = rsp0;
}

void write_tss2(GDT_Entry *gdt_entry) {
  u64 base_high = ((u64)&tss_entry) >> 32;
  gdt_entry->raw = 0;
  gdt_entry->raw = base_high;
}

void *get_current_sp(void);
void write_tss(struct GDT_Entry *gdt_entry) {
  u64 base = (u64)&tss_entry;
  u32 limit = sizeof(tss_entry);

  gdt_entry->limit_low = limit;
  gdt_entry->base_low = base;
  gdt_entry->accessed = 1;
  gdt_entry->read_write = 0;
  gdt_entry->conforming_expand_down = 0;
  gdt_entry->code = 1;
  gdt_entry->code_data_segment = 0;
  gdt_entry->DPL = 0;
  gdt_entry->present = 1;
  gdt_entry->limit_high = limit >> 16;
  gdt_entry->available = 0;
  gdt_entry->long_mode = 1;
  gdt_entry->big = 0;
  gdt_entry->gran = 0;
  gdt_entry->base_high = (base & ((u64)0xff << 24)) >> 24;

  memset(&tss_entry, 0, sizeof(tss_entry));
  // TODO: This is not required on 64 bit? Why?
  // tss_entry.ss0 = GDT_KERNEL_DATA_SEGMENT * GDT_ENTRY_SIZE;
  gdt_change_rsp0((u64)get_current_sp());
}

void flush_tss(void);
void gdt_init() {
  gdt_entries[GDT_NULL_SEGMENT].raw = 0x0;
  gdt_entries[GDT_KERNEL_CODE_SEGMENT].raw =
      0xAF9A000000FFFF; // Kernel code segment
  gdt_entries[GDT_KERNEL_DATA_SEGMENT].raw =
      0xCF92000000FFFF; // Kernel data segment

  // Usermode code segment
  memcpy(&gdt_entries[GDT_USERMODE_CODE_SEGMENT],
         &gdt_entries[GDT_KERNEL_CODE_SEGMENT], GDT_ENTRY_SIZE);

  // Usermode data segment
  memcpy(&gdt_entries[GDT_USERMODE_DATA_SEGMENT],
         &gdt_entries[GDT_KERNEL_DATA_SEGMENT], GDT_ENTRY_SIZE);

  // Set DPL to 3 to indicate that the segment is in ring 3
  gdt_entries[GDT_USERMODE_CODE_SEGMENT].s.DPL = 3;
  gdt_entries[GDT_USERMODE_DATA_SEGMENT].s.DPL = 3;

  write_tss((struct GDT_Entry *)&gdt_entries[GDT_TSS_SEGMENT]);
  write_tss2(&gdt_entries[GDT_TSS2_SEGMENT]);

  //  gdtr.address = &gdt_entries;
  // gdtr.limit = sizeof(gdt_entries) - 1;

  //  gdtr.limit = num_gdt_entries * sizeof(uint64_t) - 1;
  // gdtr.address = (uint64_t)gdt_entries;

  gdtr.limit = num_gdt_entries * sizeof(uint64_t) - 1;
  gdtr.address = (uint64_t)gdt_entries;

  load_gdt(&gdtr);
  flush_tss();
}
