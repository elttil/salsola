#include <stdbool.h>
#include <typedefs.h>

#define PCI_BAR_MEM 1
#define PCI_BAR_IO 2

struct pci_base_address_register {
  u32 address;
  u32 size;
  u8 type;
};

struct pci_general_device {
  u32 base_mem_io;
  u8 interrupt_line;
};

struct pci_device {
  u16 vendor;
  u16 device;
  u8 bus;
  u8 slot;
  u8 header_type;
  union {
    struct pci_general_device gen;
  };
};

bool pci_get_bar(const struct pci_device *device, u8 bar_index,
                 struct pci_base_address_register *bar);
u32 pci_config_read32(const struct pci_device *device, u8 func, u8 offset);
void pci_config_write32(const struct pci_device *device, u8 func, u8 offset,
                        u32 data);

int pci_populate_device_struct(u16 vendor, u16 device,
                               struct pci_device *pci_device);
bool pci_devices_by_id(u8 class_id, u8 subclass_id,
                     struct pci_device *pci_device);

void pci_enable_interrupts(const struct pci_device *device);
u8 pci_get_interrupt_line(const struct pci_device *device);
void pci_set_interrupt_line(const struct pci_device *device, u8 interrupt_line);
