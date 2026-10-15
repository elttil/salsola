#include <arch/amd64/acpi.h>
#include <mmu.h>

bool rsdt_find_signature(struct RSDT *rsdt, char *signature, void **out) {
  int entries = (rsdt->h.Length - sizeof(rsdt->h)) / 4;

  const size_t length = strlen(signature);
  for (int i = 0; i < entries; i++) {
    void *virtual = mmu_map_frames(
        (void *)rsdt->PointerToOtherSDT[i], sizeof(struct ACPISDTHeader),
        MMU_FLAG_PCD | MMU_FLAG_RW | MMU_FLAG_PRESENT);
    struct ACPISDTHeader *h = (struct ACPISDTHeader *)virtual;
    if (!strncmp(h->Signature, signature, length)) {
      PTR_ASSIGN(out, h);
      return true;
    }
    mmu_unmap_frames(virtual, sizeof(struct ACPISDTHeader), false);
  }

  return false;
}

bool rsdp_checksum(u8 *src, size_t size) {
  u8 r = 0;
  for (size_t i = 0; i < size; i++, src++) {
    r += *src;
  }
  return (0 == r);
}
