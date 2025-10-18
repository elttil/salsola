#include <assert.h>
#include <drivers/serial.h>
#include <fs/ramfs.h>
#include <fs/vfs.h>
#include <io.h>
#include <stddef.h>
#include <sv.h>

#define PORT 0x3f8 // COM1

int serial_init(void) {
  outb(PORT + 1, 0x00); // Disable all interrupts
  outb(PORT + 3, 0x80); // Enable DLAB (set baud rate divisor)
  outb(PORT + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
  outb(PORT + 1, 0x00); //                  (hi byte)
  outb(PORT + 3, 0x03); // 8 bits, no parity, one stop bit
  outb(PORT + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
  outb(PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set
  outb(PORT + 4, 0x1E); // Set in loopback mode, test the serial chip
  outb(PORT + 0, 0xAE); // Test serial chip (send byte 0xAE and check if serial
                        // returns same byte)

  // Check if serial is faulty (i.e: not same byte as sent)
  if (inb(PORT + 0) != 0xAE) {
    return 1;
  }

  // If serial is not faulty set it in normal operation mode
  // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
  outb(PORT + 4, 0x0F);
  return 0;
}

static int is_transmit_empty() {
  return inb(PORT + 5) & 0x20;
}

void serial_print_string(const char *str, size_t length) {
  for (size_t i = 0; i < length; i++) {
    for (; 0 == is_transmit_empty();)
      ;

    outb(PORT, str[i]);
  }
}

size_t serial_write(struct vfs_fd *fd, const void *buffer, size_t length,
                    size_t offset, err_t *err) {
  (void)fd;
  (void)buffer;
  (void)length;
  (void)offset;
  serial_print_string(buffer, length);
  ASSIGN_PTR(err, ERROR_SUCCESS);
  return length;
}

bool serial_open(struct vfs_fd *fd, struct sv file, int flags,
                 void *internal_object, int *err) {
  (void)fd;
  (void)file;
  (void)flags;
  (void)err;
  (void)internal_object;
  fd->write = serial_write;
  return true;
}

bool serial_add_file() {
  struct vfs_mount *mount = vfs_find_mount(C_TO_SV("/dev"));
  assert(mount);
  assert(
      ramfs_add_file(mount, C_TO_SV("/dev/serial"), serial_open, NULL, NULL));
  return true;
}

void serial_print_char(char a) {
  for (; 0 == is_transmit_empty();)
    ;

  outb(PORT, a);
}
