#include <arch/amd64/idt.h>
#include <assert.h>
#include <drivers/pci.h>
#include <fs/vfs.h>
#include <io.h>
#include <kmalloc.h>
#include <task.h>

#include <kprintf.h>

#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA 0xCFC

#define PCIFS_TYPE_CONFIG 1
#define PCIFS_TYPE_BAR 2
#define PCIFS_TYPE_INTERRUPT 3

// Gets the bar address and size the populates the struct("bar") given.
// Return value:
// 1 Success
// 0 Failure
bool pci_get_bar(const struct pci_device *device, u8 bar_index,
                 struct pci_base_address_register *bar) {
  if (bar_index > 5) {
    return false;
  }
  u8 offset = 0x10 + bar_index * sizeof(u32);
  u32 physical_bar = pci_config_read32(device, 0, offset);
  u8 type;
  if (physical_bar & 0x1) {
    type = PCI_BAR_IO;
  } else {
    type = PCI_BAR_MEM;
  }
  u32 original_bar = physical_bar;
  physical_bar &= 0xFFFFFFF0;
  // Now we do the konami code of PCI devices to figure out the size of what
  // the BAR is pointing to.

  // Comments taken from https://wiki.osdev.org/PCI#Address_and_size_of_the_BAR

  // Write a value of all 1's to the register,
  pci_config_write32(device, 0, offset, 0xFFFFFFFF);
  // then read it back.
  u32 bar_result = pci_config_read32(device, 0, offset);

  // The amount of memory can then be determined by masking the information
  // bits,
  bar_result &=
      ~(0xF); // Apparently the "information bits" are the last 4 bits according
              // to this answer: https://stackoverflow.com/a/39618552

  // performing a bitwise NOT ('~' in C),
  bar_result = ~bar_result;

  // and incrementing the value by 1.
  bar_result++;

  // Restore the old result
  pci_config_write32(device, 0, offset, original_bar);

  bar->address = physical_bar;
  bar->size = bar_result;
  bar->type = type;
  return true;
}

void pci_config_write32(const struct pci_device *device, u8 func, u8 offset,
                        u32 data) {
  u32 address;
  u32 lbus = (u32)device->bus;
  u32 lslot = (u32)device->slot;
  u32 lfunc = (u32)func;

  // Create configuration address as per Figure 1
  address = (u32)((lbus << 16) | (lslot << 11) | (lfunc << 8) |
                  (offset & 0xFC) | ((u32)0x80000000));

  // Write out the address
  outl(CONFIG_ADDRESS, address);
  outl(CONFIG_DATA, data);
}

u32 pci_config_read32(const struct pci_device *device, u8 func, u8 offset) {
  u32 address;
  u32 lbus = (u32)device->bus;
  u32 lslot = (u32)device->slot;
  u32 lfunc = (u32)func;

  // Create configuration address as per Figure 1
  address = (u32)((lbus << 16) | (lslot << 11) | (lfunc << 8) |
                  (offset & 0xFC) | ((u32)0x80000000));

  // Write out the address
  outl(CONFIG_ADDRESS, address);
  return inl(CONFIG_DATA);
}

bool pci_devices_by_id(u8 class_id, u8 subclass_id,
                       struct pci_device *pci_device) {
  for (u8 bus = 0; bus < 255; bus++) {
    for (u8 slot = 0; slot < 255; slot++) {
      pci_device->bus = bus;
      pci_device->slot = slot;
      u16 class_info = pci_config_read32(pci_device, 0, 0x8) >> 16;
      u16 h_classcode = (class_info & 0xFF00) >> 8;
      u16 h_subclass = (class_info & 0x00FF);
      if (h_classcode != class_id) {
        continue;
      }
      if (h_subclass != subclass_id) {
        continue;
      }

      u32 device_vendor = pci_config_read32(pci_device, 0, 0);
      pci_device->vendor = (device_vendor & 0xFFFF);
      pci_device->device = (device_vendor >> 16);

      u32 BIST_headertype_latencytimer_cachesize =
          pci_config_read32(pci_device, 0, 0xC);
      pci_device->header_type = BIST_headertype_latencytimer_cachesize >> 16;
      pci_device->header_type &= 0xFF;
      return true;
    }
  }
  return false;
}

// /dev/pci/<vendor>/<device>/struct
// /dev/pci/<vendor>/<device>/bar0
// /dev/pci/<vendor>/<device>/bar1
// /dev/pci/<vendor>/<device>/bar2

err_t pcifs_read_config(struct vfs_fd *fd, void *buffer, size_t length,
                        size_t _offset, size_t *rc) {
  struct pci_device *device = (struct pci_device *)fd->internal_object;
  u8 func = (_offset >> 8) & 0xFF;
  u8 offset = (_offset) & 0xFF;

  u32 v = pci_config_read32(device, func, offset);
  size_t ol = min(length, sizeof(u32));
  memcpy(buffer, &v, ol);
  ASSIGN_PTR(rc, ol);
  return ERROR_SUCCESS;
}

err_t pcifs_write_config(struct vfs_fd *fd, const void *buffer, size_t length,
                         size_t _offset, size_t *rc) {
  struct pci_device *device = (struct pci_device *)fd->internal_object;
  u8 func = (_offset >> 8) & 0xFF;
  u8 offset = (_offset) & 0xFF;

  u32 v = 0;
  size_t ol = min(length, sizeof(u32));
  memcpy(&v, buffer, ol);

  pci_config_write32(device, func, offset, v);
  ASSIGN_PTR(rc, ol);
  return ERROR_SUCCESS;
}

struct pcifs_interrupt {
  struct list_fd_ctx listeners;
  bool has_interrupt;
  bool enabled;
};

struct pcifs_interrupt lines[0xFF] = {0};
rwlock_t lines_lock;

void pcifs_handler(struct cpu_status *r) {
  u8 line = r->vector_number - 0x20;
  rwlock_write_acquire(&lines_lock);
  if (!lines[line].enabled) {
    rwlock_write_release(&lines_lock);
    return;
  }
  lines[line].has_interrupt = true;

  for (u64 i = 0; i < lines[line].listeners.length; i++) {
    struct vfs_fd *fd;
    assert(list_fd_get(&lines[line].listeners, i, &fd));
    if (!fd) {
      continue;
    }
    fd->data.can_read = true;
    fd->data.can_write = true;
    vfs_notify_listeners(fd);
  }
  rwlock_write_release(&lines_lock);
}

void pcifs_enable_line(u8 line, struct vfs_fd *fd) {
  rwlock_write_acquire(&lines_lock);
  if (lines[line].enabled) {
    assert(list_fd_add(&lines[line].listeners, fd, NULL));
    rwlock_write_release(&lines_lock);
    return;
  }

  lines[line].enabled = true;
  lines[line].has_interrupt = false;
  list_fd_init(&lines[line].listeners);
  assert(list_fd_add(&lines[line].listeners, fd, NULL));
  rwlock_write_release(&lines_lock);

  handler_install(0x20 + line, pcifs_handler, 0);
}

err_t pcifs_read_interrupt(struct vfs_fd *fd, void *buffer, size_t length,
                           size_t offset, size_t *rc) {
  (void)buffer;
  (void)length;
  (void)offset;
  ASSIGN_PTR(rc, 0);
  u8 line = (u8)fd->internal_object;
  rwlock_read_acquire(&lines_lock);
  if (!lines[line].enabled) {
    rwlock_read_release(&lines_lock);
    assert(0); // This should never happen.
  }
  if (!lines[line].has_interrupt) {
    rwlock_read_release(&lines_lock);
    return ERROR_READ_WOULD_BLOCK;
  }
  rwlock_read_release(&lines_lock);
  return ERROR_SUCCESS;
}

err_t pcifs_write_interrupt(struct vfs_fd *fd, const void *buffer,
                            size_t length, size_t offset, size_t *rc) {
  (void)buffer;
  (void)length;
  (void)offset;
  ASSIGN_PTR(rc, 0);
  u8 line = (u8)fd->internal_object;
  rwlock_write_acquire(&lines_lock);
  if (!lines[line].enabled) {
    rwlock_write_release(&lines_lock);
    assert(0); // This should never happen.
  }
  if (!lines[line].has_interrupt) {
    rwlock_write_release(&lines_lock);
    return ERROR_READ_WOULD_BLOCK;
  }
  lines[line].has_interrupt = false;
  eoi(line);
  rwlock_write_release(&lines_lock);
  return ERROR_SUCCESS;
}

err_t pcifs_read_bar(struct vfs_fd *fd, void *buffer, size_t length,
                     size_t offset, size_t *rc) {
  const struct pci_base_address_register *bar =
      (struct pci_base_address_register *)fd->internal_object;

  assert(bar->type == PCI_BAR_IO);
  if (length > sizeof(u32)) {
    // TODO: Check if this is "correct".
    return ERROR_WRITE_EXCEEDS_BOUNDS;
  }
  if (length + offset > bar->size) {
    return ERROR_WRITE_EXCEEDS_BOUNDS;
  }

  if (length == sizeof(u32)) {
    u32 v;
    v = inl(bar->address + offset);
    memcpy(buffer, &v, sizeof(v));
  } else if (length == sizeof(u16)) {
    u16 v;
    v = inw(bar->address + offset);
    memcpy(buffer, &v, sizeof(v));
  } else if (length == sizeof(u8)) {
    u8 v;
    v = inb(bar->address + offset);
    memcpy(buffer, &v, sizeof(v));
  } else {
    return ERROR_BUFFER_TOO_SMALL;
  }
  ASSIGN_PTR(rc, length);
  return ERROR_SUCCESS;
}

err_t pcifs_write_bar(struct vfs_fd *fd, const void *buffer, size_t length,
                      size_t offset, size_t *rc) {
  const struct pci_base_address_register *bar =
      (struct pci_base_address_register *)fd->internal_object;

  assert(bar->type == PCI_BAR_IO);
  if (length > sizeof(u32)) {
    // TODO: Check if this is "correct".
    assert(0);
    return ERROR_WRITE_EXCEEDS_BOUNDS;
  }
  if (length + offset > bar->size) {
    assert(0);
    return ERROR_WRITE_EXCEEDS_BOUNDS;
  }

  if (length == sizeof(u32)) {
    u32 v;
    memcpy(&v, buffer, sizeof(v));
    outl(bar->address + offset, v);
  } else if (length == sizeof(u16)) {
    u16 v;
    memcpy(&v, buffer, sizeof(v));
    outw(bar->address + offset, v);
  } else if (length == sizeof(u8)) {
    u8 v;
    memcpy(&v, buffer, sizeof(v));
    outb(bar->address + offset, v);
  } else {
    assert(0);
    return ERROR_BUFFER_TOO_SMALL;
  }
  ASSIGN_PTR(rc, length);
  return ERROR_SUCCESS;
}

err_t pcifs_lseek(struct vfs_fd *fd, off_t offset, int whence, off_t *out) {
  off_t ret_offset = fd->offset;
  switch (whence) {
  case SEEK_SET:
    ret_offset = offset;
    break;
  case SEEK_CUR:
    ret_offset += offset;
    break;
  case SEEK_END:
    assert(0);
    break;
  default:
    return ERROR_INVALID_WHENCE;
    break;
  }
  fd->offset = ret_offset;
  ASSIGN_PTR(out, ret_offset);
  return ERROR_SUCCESS;
}

struct vfs_fd *pcifs_open(struct vfs_mount *mount, struct sv file, int flags,
                          int *err) {
  (void)mount;
  (void)flags;
  ASSIGN_PTR(err, ERROR_SUCCESS);

  struct sv sv_vendor = sv_split_delim(file, &file, '/');
  struct sv sv_device = sv_split_delim(file, &file, '/');
  if (0 == sv_length(sv_vendor) || 0 == sv_length(sv_device)) {
    goto pcifs_open_error;
  }

  int got_num;
  u64 vendor = sv_parse_unsigned_number(sv_vendor, NULL, &got_num);
  if (!got_num) {
    goto pcifs_open_error;
  }
  u64 device = sv_parse_unsigned_number(sv_device, NULL, &got_num);
  if (!got_num) {
    goto pcifs_open_error;
  }
  if (vendor > 0xFFFF || device > 0xFFFF) {
    goto pcifs_open_error;
  }

  struct pci_device tmp;
  if (!pci_populate_device_struct(vendor, device, &tmp)) {
    goto pcifs_open_error;
  }

  u8 interrupt_line;
  struct pci_base_address_register *bar = NULL;
  u32 type;
  if (sv_eq(file, C_TO_SV("config"))) {
    type = PCIFS_TYPE_CONFIG;
  } else if (sv_eq(file, C_TO_SV("interrupt"))) {
    type = PCIFS_TYPE_INTERRUPT;
  } else if (sv_eq(file, C_TO_SV("bar0")) || sv_eq(file, C_TO_SV("bar1"))) {
    type = PCIFS_TYPE_BAR;
    bar = kmalloc(sizeof(struct pci_base_address_register));
    assert(bar);

    // TODO: Make this more generic
    int bar_type;
    if (sv_eq(file, C_TO_SV("bar0"))) {
      bar_type = 0;
    } else if (sv_eq(file, C_TO_SV("bar1"))) {
      bar_type = 1;
    } else {
      assert(0);
    }

    if (!pci_get_bar(&tmp, bar_type, bar)) {
      kfree(bar);
      goto pcifs_open_error;
    }
  } else {
    goto pcifs_open_error;
  }

  struct vfs_fd *fd = vfs_allocate_fd();
  if (PCIFS_TYPE_CONFIG == type) {
    fd->read = pcifs_read_config;
    fd->write = pcifs_write_config;
    struct pci_device *pci_device = kmalloc(sizeof(struct pci_device));
    assert(pci_device);
    memcpy(pci_device, &tmp, sizeof(tmp));
    fd->internal_object = pci_device;
  } else if (PCIFS_TYPE_BAR == type) {

    u32 bar0 = pci_config_read32(&tmp, 0, 0x10);
    assert(bar0 & 0x1 && "Only support memory IO");
    tmp.gen.base_mem_io = bar0 & (~0x3);

    fd->internal_object = bar;
    fd->read = pcifs_read_bar;
    fd->write = pcifs_write_bar;
  } else if (PCIFS_TYPE_INTERRUPT == type) {
    interrupt_line = pci_get_interrupt_line(&tmp);
    fd->internal_object = (void *)interrupt_line;
    fd->read = pcifs_read_interrupt;
    fd->write = pcifs_write_interrupt;
    pcifs_enable_line(interrupt_line, fd);
    pci_enable_interrupts(&tmp);
  }
  fd->lseek = pcifs_lseek;
  return fd;

pcifs_open_error:
  ASSIGN_PTR(err, ERROR_NO_FILE);
  return NULL;
}

struct vfs_mount *pcifs_create(void) {
  struct vfs_mount *mount = kmalloc(sizeof(struct vfs_mount));
  if (!mount) {
    return NULL;
  }
  mount->open = pcifs_open;
  return mount;
}

int pci_populate_device_struct(u16 vendor, u16 device,
                               struct pci_device *pci_device) {
  pci_device->vendor = vendor;
  pci_device->device = device;

  for (int bus = 0; bus < 256; bus++) {
    for (int slot = 0; slot < 256; slot++) {
      struct pci_device tmp;
      tmp.bus = bus;
      tmp.slot = slot;
      u32 device_vendor = pci_config_read32(&tmp, 0, 0);
      if (vendor != (device_vendor & 0xFFFF)) {
        continue;
      }
      if (device != (device_vendor >> 16)) {
        continue;
      }
      pci_device->bus = bus;
      pci_device->slot = slot;
      u32 bar0 = pci_config_read32(pci_device, 0, 0x10);
      assert(bar0 & 0x1 && "Only support memory IO");
      pci_device->gen.base_mem_io = bar0 & (~0x3);
      return 1;
    }
  }
  return 0;
}

void pci_enable_interrupts(const struct pci_device *device) {
  u32 register1 = pci_config_read32(device, 0, 0x4);
  u8 current_interrupt = (register1 >> 10) & 1;
  if (current_interrupt) {
    kprintf("PCI: Interrrupt already enabled\n");
    return;
  }
  register1 |= (1 << 10);
  pci_config_write32(device, 0, 0x4, register1);
}

void pci_set_interrupt_line(const struct pci_device *device,
                            u8 interrupt_line) {
  u32 register1 = pci_config_read32(device, 0, 0x3C);
  register1 &= ~(0xFF);
  register1 |= interrupt_line;
  pci_config_write32(device, 0, 0x3C, register1);
}

u8 pci_get_interrupt_line(const struct pci_device *device) {
  return pci_config_read32(device, 0, 0x3C) & 0xFF;
}
