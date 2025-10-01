#include <arch/amd64/msr.h>
#include <arch/amd64/regs.h>
#include <arch/amd64/smp.h>
#include <assert.h>
#include <kprintf.h>
#include <lock.h>
#include <mmu.h>
#include <string.h>
#include <typedefs.h>

lock_t smp_lock;

struct ACPISDTHeader {
  char Signature[4];
  uint32_t Length;
  uint8_t Revision;
  uint8_t Checksum;
  char OEMID[6];
  char OEMTableID[8];
  uint32_t OEMRevision;
  uint32_t CreatorID;
  uint32_t CreatorRevision;
} __attribute__((packed));

struct RSDT {
  struct ACPISDTHeader h;
  uint32_t PointerToOtherSDT[0]; // (h.Length - sizeof(h)) / 4;
} __attribute__((packed));

struct processor_local_apic {
  u8 apic_processor_id;
  u8 apic_id;
  u32 flags;
} __attribute__((packed));

struct madt_entry {
  u8 entry_type;
  u8 record_length;
  union {
    struct processor_local_apic local_apic;
  };
};

struct MADT {
  struct ACPISDTHeader h;
  u32 local_apic_address;
  u32 flags;
  struct madt_entry entries[0];
} __attribute__((packed));

struct RSDP_t {
  char Signature[8];
  uint8_t Checksum;
  char OEMID[6];
  uint8_t Revision;
  uint32_t RsdtAddress;
} __attribute__((packed));

bool rsdp_checksum(u8 *src, size_t size) {
  u8 r = 0;
  for (size_t i = 0; i < size; i++, src++) {
    r += *src;
  }
  return (0 == r);
}

u8 *lapic_ptr = NULL;

void mdelay(int s) {
  (void)s;
  //  for (int i = 0; i < 10000; i++) {
  //    kprintf(".");
  //  }
}

void udelay(int s) {
  (void)s;
  //  for (int i = 0; i < 100; i++) {
  //    kprintf(".");
  //  }
}

// void ap_trampoline();
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

  uint64_t cr3 = get_cr3();
  // uint64_t cr3;
  //__asm__ __volatile__("mov %%cr3, %%rbx" : "=b"(cr3) : :);

  volatile u32 *ptr = (u32 *)((uintptr_t)&trampoline_cr3 + 0xFFFFFF8000000000);

  *ptr = cr3;
  kprintf("*ptr: %x\n", *ptr);
  kprintf("cr3: %x\n", cr3);
  //  for (;;)
  //    ;

  int i = core;
  kprintf("bspid: %d\n", bspid);
  // do not start BSP, that's already running this code
  // #if 0
  if (core == bspid) { // FIXME: Incorrect
    kprintf("CORE Already in use\n");
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

bool rsdt_find_signature(struct RSDT *rsdt, char *signature, void **out) {
  int entries = (rsdt->h.Length - sizeof(rsdt->h)) / 4;

  const size_t length = strlen(signature);
  for (int i = 0; i < entries; i++) {
    void *virtual = mmu_map_frames((void *)rsdt->PointerToOtherSDT[i],
                                   sizeof(struct ACPISDTHeader));
    struct ACPISDTHeader *h = (struct ACPISDTHeader *)virtual;
    kprintf("Signature: %.*s\n", 4, h->Signature);
    if (!strncmp(h->Signature, signature, length)) {
      PTR_ASSIGN(out, h);
      return true;
    }
    mmu_unmap_frames(virtual, sizeof(struct ACPISDTHeader));
  }

  return false;
}

void enable_core_asm(u64 rdi);

u8 core_id_get(void) {
  return bspid_get();
}

void core_main() {
  lock_release(&smp_lock);
  mmu_remove_identity();

  kprintf("CORE MAIN\n");
  for (;;)
    ;
}

void gdt_init();
void ap_startup() {
  kprintf("\nap_startup bspid: %d\n", bspid_get());
  gdt_init();
  mmu_init_for_new_core(core_main);
  for (;;)
    ;
}

void smp_init(struct multiboot_tag *tags) {
  for (struct multiboot_tag *tag = tags; tag->type != MULTIBOOT_TAG_TYPE_END;
       tag = (struct multiboot_tag *)((multiboot_uint8_t *)tag +
                                      ((tag->size + 7) & ~7))) {
    if (tag->type != MULTIBOOT_TAG_TYPE_ACPI_OLD) {
      kprintf("tag->type: %d\n", tag->type);
      continue;
    }

    struct multiboot_tag_old_acpi *m = (struct multiboot_tag_old_acpi *)tag;
    kprintf("m->type: %d\n", m->type);
    kprintf("m->size: %d\n", m->size);
    assert(m->size >= sizeof(struct RSDP_t));
    struct RSDP_t *rsdp = (struct RSDP_t *)m->rsdp;
    kprintf("Signature: %.*s\n", 8, rsdp->Signature);
    kprintf("Revision: %d\n", rsdp->Revision);

    assert(rsdp_checksum((void *)rsdp, sizeof(*rsdp)));

    void *mapped_frames =
        mmu_map_frames((void *)rsdp->RsdtAddress, sizeof(struct ACPISDTHeader));
    kprintf("%x\n", mapped_frames);
    kprintf("%x\n", rsdp->RsdtAddress);

    struct RSDT *header = (struct RSDT *)mapped_frames;
    kprintf("Header Signature: %.*s\n", 4, header->h.Signature);
    // TODO: Make sure this check does not pass page boundaries(the
    // length could be corrupted)
    assert(rsdp_checksum((void *)header, header->h.Length));
    kprintf("After checksum\n");

    struct MADT *madt;
    assert(rsdt_find_signature(header, "APIC", (void **)&madt));
    kprintf("MADT Signature: %.*s\n", 4, madt->h.Signature);
    kprintf("Local APIC: %p\n", madt->local_apic_address);
    // lapic_ptr
    lapic_ptr = mmu_map_frames((void *)madt->local_apic_address, 0x1000);

    for (struct madt_entry *p = madt->entries;
         ((uintptr_t)p - (uintptr_t)madt) < madt->h.Length;) {
      kprintf("entry_type: %x\n", p->entry_type);
      if (0 == p->entry_type) {
        kprintf("id: %x\n", p->local_apic.apic_processor_id);
        enable_core(p->local_apic.apic_processor_id);
      }

      p = (struct madt_entry *)((uintptr_t)p + p->record_length);
    }
  }
  lock_acquire(&smp_lock);
}
