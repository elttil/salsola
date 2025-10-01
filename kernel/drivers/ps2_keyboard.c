#include <drivers/ps2_keyboard.h>
#include <io.h>
#include <kprintf.h>
#include <ringbuffer.h>
#include <stdbool.h>

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

struct ringbuffer keyboard_buffer;

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

void keyboard_handler(struct cpu_status *r) {
  (void)r;
  u16 c;
  c = inb(PS2_REG_DATA);
  outb(0x20, 0x20);

  int released = 0;
  if (c & 0x80) {
    switch ((c & ~(0x80)) & 0xFF) {
    case 0x2A: // Left shift
    case 0x36: // Right shift
      is_shift_down = false;
      return;
    case 0x38:
      is_alt_down = false;
      return;
    case 0x1D:
      is_ctrl_down = false;
      return;
    }
    released = 1;
  } else {
    switch (c & 0xFF) {
    case 0x2A: // Left shift
    case 0x36: // Right shift
      is_shift_down = true;
      return;
    case 0x38:
      is_alt_down = true;
      return;
    case 0x1D:
      is_ctrl_down = true;
      return;
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
  //  ringbuffer_write(&keyboard_buffer, (u8 *)&ev, sizeof(ev));
}

bool ps2_keyboard_init(void) {
  //  if (!ringbuffer_init(&keyboard_buffer, sizeof(struct key_event) * 128)) {
  //    return false;
  //  }
  handler_install(0x21, keyboard_handler);
  return true;
}
