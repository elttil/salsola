#include <arch/amd64/acpi.h>
#include <arch/amd64/hpet.h>
#include <arch/amd64/msr.h>
#include <assert.h>
#include <mmu.h>

#include <kprintf.h>

struct address_structure {
  uint8_t address_space_id; // 0 - system memory, 1 - system I/O
  uint8_t register_bit_width;
  uint8_t register_bit_offset;
  uint8_t reserved;
  uint64_t address;
} __attribute__((packed));

struct HPET {
  struct ACPISDTHeader h;
  uint8_t hardware_rev_id;
  uint8_t comparator_count : 5;
  uint8_t counter_size : 1;
  uint8_t reserved : 1;
  uint8_t legacy_replacement : 1;
  uint16_t pci_vendor_id;
  struct address_structure address;
  uint8_t hpet_number;
  uint16_t minimum_tick;
  uint8_t page_protection;
} __attribute__((packed));

// FIXME/NOTE: Something strange happens when this function is
// compiled with O3+LTO. Unsure why.
__attribute__((optimize("-O0"))) u64
hpet_get_tsc_mhz(struct multiboot_tag *tags) {
  for (struct multiboot_tag *tag = tags; tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = (struct multiboot_tag *)((multiboot_uint8_t *)tag +
                                      ((tag->size + 7) & ~7))) {
    if (tag->type != MULTIBOOT_TAG_TYPE_ACPI_OLD &&
        tag->type != MULTIBOOT_TAG_TYPE_ACPI_NEW) {
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

    struct HPET *hpet;
    assert(rsdt_find_signature(header, "HPET", (void **)&hpet));

    bool is_io = hpet->address.address_space_id;
    assert(!is_io);

    volatile void *virtual = mmu_map_frames(
        (void *)hpet->address.address,
        PAGE_SIZE /*TODO: Figure out the real size(It doesn't actually matter)*/
        ,
        MMU_FLAG_PCD | MMU_FLAG_RW | MMU_FLAG_PRESENT);

    volatile u64 *main_counter =
        (volatile u64 *)((volatile u8 *)virtual + 0x0F0);
    volatile u64 *cfg = (volatile u64 *)((volatile u8 *)virtual + 0x010);

    u64 cap = *(volatile u64 *)virtual;
    u32 COUNTER_CLK_PERIOD = cap >> 32;

    // TODO: Maybe use something more precise.
    u32 frequency_ns = COUNTER_CLK_PERIOD / 1000000;

    // Disable
    *cfg &= ~((u64)(1 << 0));

    *main_counter = hpet->minimum_tick; // TODO: Can this just be zero?
    volatile u64 start = *main_counter;

    // Enable
    *cfg |= ((u64)(1 << 0));

    // Quickly read
    volatile u64 s = rdtsc();

    // Busywork to even out overhead.
    for (volatile int i = 0; i < 1000000; i++)
      ;

    // Disable
    *cfg &= ~((u64)(1 << 0));
    volatile u64 end = *main_counter;

    // Quickly read
    volatile u64 d = rdtsc();
    u64 diff_tsc = d - s;
    u64 diff_nanoseconds = (end - start) * frequency_ns;

    u64 mhz = (diff_tsc * 1000) / diff_nanoseconds;

    mmu_unmap_frames(mapped_frames, sizeof(struct ACPISDTHeader), false);
    mmu_unmap_frames((void *)virtual, PAGE_SIZE /*NOTE: See above TODO */,
                     false);
    return mhz;
  }
  return 0;
}
