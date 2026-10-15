#include <typedefs.h>
#include <stdbool.h>
#include <stddef.h>

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

struct RSDT {
  struct ACPISDTHeader h;
  uint32_t PointerToOtherSDT[0]; // (h.Length - sizeof(h)) / 4;
} __attribute__((packed));

struct RSDP_t {
  char Signature[8];
  uint8_t Checksum;
  char OEMID[6];
  uint8_t Revision;
  uint32_t RsdtAddress;
} __attribute__((packed));

bool rsdt_find_signature(struct RSDT *rsdt, char *signature, void **out);
bool rsdp_checksum(u8 *src, size_t size);
