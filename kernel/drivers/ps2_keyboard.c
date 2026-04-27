#include <assert.h>
#include <drivers/ps2_keyboard.h>
#include <fs/ramfs.h>
#include <fs/vfs.h>
#include <io.h>
#include <log.h>
#include <ringbuffer.h>
#include <stdbool.h>
#include <sv.h>
#include <task.h>

#include <kprintf.h>

#define PS2_REG_DATA 0x60
#define PS2_REG_STATUS 0x64
#define PS2_REG_COMMAND 0x64

#define PS2_CMD_ENABLE_FIRST_PORT 0xAE // no rsp

#define PS2_CMD_TEST_CONTROLLER 0xAA // has rsp

#define PS2_RSP_TEST_PASSED 0x55
#define PS2_RSP_TEST_FAILED 0xFC

#define PS2_CMD_SET_SCANCODE 0xF0 // has rsp
#define PS2_KB_ACK 0xFA
#define PS2_KB_RESEND 0xFE

#define PS2_CMD_SET_MAKE_RELEASE 0xF8 // has rsp

u8 kb_scancodes[3] = {0x43, 0x41, 0x3f};

bool is_shift_down = false;
bool is_alt_down = false;
bool is_ctrl_down = false;

struct key_event {
  char c;
  u8 mode;    // (shift (0 bit)) (alt (1 bit)) (ctrl (2 bit))
  u8 release; // 0 pressed, 1 released
};

u8 ascii_table[] = {
    'e', '\x1B', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8,
    '\t',

    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    //	0, // [
    //	0, // ]
    //	0,
    //	0, // ?
    '[', ']',
    '\n', // ENTER
    'C',

    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    ';',  // ;
    '\'', // ;
    '`',  // ;
    'D',  // LEFT SHIFT
    '\\', // ;
    'z', 'x', 'c', 'v', 'b', 'n', 'm',
    ',', // ;
    '.', // ;
    '/', // ;
    'U', // ;
    'U', // ;
    'U', // ;
    ' ', // ;
};

u8 capital_ascii_table[] = {
    'e', '\x1B', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8,
    '\t',

    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    //	0, // [
    //	0, // ]
    //	0,
    //	0, // ?
    '{', '}',
    '\n', // ENTER
    'C',

    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    ':',  // ;
    '\"', // ;
    '~',  // ;
    'D',  // LEFT SHIFT
    '|',  // ;
    'Z', 'X', 'C', 'V', 'B', 'N', 'M',
    '<', // ;
    '>', // ;
    '?', // ;
    'U', // ;
    'U', // ;
    'U', // ;
    ' ', // ;
};

static struct ringbuffer keyboard_buffer;

u8 keyboard_to_ascii(u16 key, u8 capital) {
  if ((key & 0xFF) > sizeof(ascii_table)) {
    return 'U';
  }
  if (capital) {
    return capital_ascii_table[key & 0xFF];
  } else {
    return ascii_table[key & 0xFF];
  }
}

static struct list_fd_ctx listeners;

void keyboard_handler(struct cpu_status *r) {
  __asm__("cli");
  (void)r;

  u16 c = inb(PS2_REG_DATA);
  bool key_set = true;
  switch (c & 0xFF) {
  case 0x2A:
  case 0x36:
  case 0x38:
  case 0x1D:
    key_set = false;
    break;
  }

  int released = 0;
  if (c & 0x80) {
    switch ((c & ~(0x80)) & 0xFF) {
    case 0x2A: // Left shift
    case 0x36: // Right shift
      is_shift_down = false;
      break;
    case 0x38:
      is_alt_down = false;
      break;
    case 0x1D:
      is_ctrl_down = false;
      break;
    }
    released = 1;
  } else {
    switch (c & 0xFF) {
    case 0x2A: // Left shift
    case 0x36: // Right shift
      is_shift_down = true;
      break;
    case 0x38:
      is_alt_down = true;
      break;
    case 0x1D:
      is_ctrl_down = true;
      break;
    }
    released = 0;
  }
  unsigned char a = keyboard_to_ascii((c & ~(0x80)) & 0xFF, is_shift_down);

  struct key_event ev;
  ev.c = a;
  ev.release = released;
  ev.mode = 0;
  ev.mode |= is_shift_down << 0;
  ev.mode |= is_alt_down << 1;
  ev.mode |= is_ctrl_down << 2;
  ev.mode |= key_set << 3;
  ringbuffer_write(&keyboard_buffer, (u8 *)&ev, sizeof(ev));
  bool has_data = (0 != ringbuffer_used(&keyboard_buffer));
  for (u64 i = 0; i < listeners.length; i++) {
    struct vfs_fd *fd;
    assert(list_fd_get(&listeners, i, &fd));
    if (!fd) {
      continue;
    }
    vfs_notify_can_read(fd, has_data);
  }
  eoi(1);
}

err_t keyboard_read(struct vfs_fd *fd, void *buffer, size_t length,
                    size_t offset, size_t *rc) {
  (void)fd;
  (void)offset;
  size_t num_entries = length / sizeof(struct key_event);
  if (0 == num_entries) {
    ASSIGN_PTR(rc, 0);
    return ERROR_SUCCESS; // TODO: Maybe provide a different error here?
  }
  err_t err = ringbuffer_wrapped_read(
      &keyboard_buffer, buffer, num_entries * sizeof(struct key_event), rc);
  bool has_data = (0 != ringbuffer_used(&keyboard_buffer));
  vfs_notify_can_read(fd, has_data);
  return err;
}

bool keyboard_open(struct vfs_fd *fd, struct sv file, int flags,
                   void *internal_object, int *err) {
  (void)fd;
  (void)file;
  (void)flags;
  (void)err;
  fd->internal_object = internal_object;
  fd->type = VFS_TYPE_CHAR_DEVICE;
  fd->read = keyboard_read;
  assert(list_fd_add(&listeners, fd, NULL));
  return true;
}

bool add_keyboard_device(struct sv filename) {
  struct vfs_mount *mount = vfs_find_mount(C_TO_SV("/dev"));
  assert(mount);
  assert(ramfs_add_file(mount, filename, keyboard_open, NULL, NULL));
  return true;
}

void simple_sleep(u64 c) {
  volatile u64 i = 0;
  for (; i < c; i++) {
    if (i == c) {
      break;
    }
  }
}

void ps2_wait_for_write() {
  for (size_t i = 0; i < 1000000; i++) {
    if (!(inb(PS2_REG_STATUS) & (1 << 1))) {
      return;
    }
  }
}

void ps2_wait_for_read() {
  for (size_t i = 0; i < 1000000; i++) {
    if ((inb(PS2_REG_STATUS) & (1 << 0))) {
      return;
    }
  }
}

static void ps2_command(u8 command) {
  ps2_wait_for_write();
  outb(PS2_REG_COMMAND, command);
}

static u8 ps2_command_call(u8 command) {
  ps2_wait_for_write();
  outb(PS2_REG_COMMAND, command);
  ps2_wait_for_read();
  return inb(PS2_REG_DATA);
}

static u8 ps2_mouse_call_arg(u8 command) {
  ps2_wait_for_write();
  outb(PS2_REG_COMMAND, 0xD4);
  ps2_wait_for_write();
  outb(PS2_REG_DATA, command);
  ps2_wait_for_read();
  return inb(PS2_REG_DATA);
}

void ps2_disable_ports() {
  ps2_command(0xAD);
  ps2_command(0xA7);
}

void ps2_enable_ports() {
  ps2_command(0xAE);
  ps2_command(0xA8);
}

u8 read_config() {
  ps2_wait_for_write();
  outb(PS2_REG_COMMAND, 0x20);
  ps2_wait_for_read();
  return inb(PS2_REG_DATA);
}

void write_config(u8 config) {
  ps2_wait_for_write();
  outb(PS2_REG_COMMAND, 0x60);
  ps2_wait_for_write();
  outb(PS2_REG_DATA, config);
}

void nop_handler(struct cpu_status *r) {
  (void)r;
  return;
}

bool ps2_keyboard_init(void) {
  u8 result;
  u8 config;

  list_fd_init(&listeners);
  if (!ringbuffer_init(&keyboard_buffer, sizeof(struct key_event) * 128)) {
    return false;
  }
  handler_install(0x21, keyboard_handler, 0);
  handler_install(0x27, nop_handler, 0);
  add_keyboard_device(C_TO_SV("/dev/keyboard"));

  ps2_disable_ports();

  size_t timeout = 1024;
  for (; timeout > 0;) {
    timeout--;
    result = inb(PS2_REG_DATA);
  }

  config = read_config();
  config &= ~(1 << 0);
  config &= ~(1 << 6);
  config &= ~(1 << 4);
  write_config(config);

  result = ps2_command_call(0xAA);
  if (0x55 != result) {
    klog(LOG_ERROR, "PS2: Self test failed. Expected: 0x55, Got: %02x\n",
         result);
    return false;
  }

  bool is_dual_channel;

  ps2_command(0xA8);
  config = read_config();
  is_dual_channel = !(config & (1 << 5));
  if (is_dual_channel) {
    config &= ~(1 << 1);
    config &= ~(1 << 5);
    write_config(config);
  }

  if (0x00 != (result = ps2_command_call(0xAB))) {
    klog(LOG_ERROR, "PS2 First Port: Test failed. Expected: 0x00, Got: %02x\n",
         result);
    return false;
  }
  if (is_dual_channel && 0x00 != (result = ps2_command_call(0xA9))) {
    klog(LOG_ERROR, "PS2 Second Port: Test failed. Expected: 0x00, Got: %02x\n",
         result);
    return false;
  }

  // Set default configuration for mouse
  if (0xFA != (result = ps2_mouse_call_arg(0xF6))) {
    klog(LOG_ERROR,
         "PS2 Mouse: Set default values failed. Expected: 0xFA, Got: %02x\n",
         result);
    //    return false;
  }
  // Enable mouse
  if (0xFA != (result = ps2_mouse_call_arg(0xF4))) {
    klog(LOG_ERROR, "PS2 Mouse: Enable failed. Expected: 0xFA, Got: %02x\n",
         result);
    //    return false;
  }

  ps2_enable_ports();

  config = read_config();
  config |= (1 << 0) | (1 << 6);
  // Enable PS2 Mouse
  config |= (1 << 1);
  write_config(config);

  return true;
}
