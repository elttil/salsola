#include <arch/amd64/idt.h>
#include <assert.h>
#include <drivers/ps2_mouse.h>
#include <fs/ramfs.h>
#include <fs/vfs.h>
#include <io.h>
#include <log.h>
#include <ringbuffer.h>
#include <stdbool.h>
#include <sv.h>
#include <task.h>

static u8 mouse_cycle = 0;
static u8 mouse_u8[3];
static int mouse_x = 0;
static int mouse_y = 0;

static u8 mouse_left_button = 0;
static u8 mouse_middle_button = 0;
static u8 mouse_right_button = 0;

static struct ringbuffer mouse_buffer;
static struct list_fd_ctx listeners;

struct mouse_event {
  u8 buttons;
  int x;
  int y;
};

static struct mouse_event previous_event = {
    .buttons = 0,
    .x = 0,
    .y = 0,
};

err_t mouse_read(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                 size_t *rc) {
  (void)fd;
  (void)offset;
  mouse_middle_button = mouse_u8[0] & (1 << 2);
  mouse_right_button = mouse_u8[0] & (1 << 1);
  mouse_left_button = mouse_u8[0] & (1 << 0);

  u32 _rc = ringbuffer_read(&mouse_buffer, buffer, length);
  if (0 == _rc && length > 0) {
    if (mouse_x != previous_event.x || mouse_y != previous_event.y) {
      int read_len = min(length, sizeof(struct mouse_event));

      struct mouse_event e;
      e.x = mouse_x;
      e.y = mouse_y;
      e.buttons = 0;
      if (mouse_middle_button) {
        e.buttons |= (1 << 2);
      }
      if (mouse_right_button) {
        e.buttons |= (1 << 1);
      }
      if (mouse_left_button) {
        e.buttons |= (1 << 0);
      }
      memcpy(buffer, &e, read_len);
      previous_event = e;
      ASSIGN_PTR(rc, read_len);

      return ERROR_SUCCESS;
    }
    return ERROR_READ_WOULD_BLOCK;
  }
  ASSIGN_PTR(rc, _rc);

  bool has_data = (0 != ringbuffer_used(&mouse_buffer));
  vfs_notify_can_read(fd, has_data);
  return ERROR_SUCCESS;
}

bool mouse_open(struct vfs_fd *fd, struct sv file, int flags,
                void *internal_object, int *err) {
  (void)fd;
  (void)file;
  (void)flags;
  (void)err;
  fd->internal_object = internal_object;
  fd->type = VFS_TYPE_CHAR_DEVICE;
  fd->read = mouse_read;
  assert(list_fd_add(&listeners, fd, NULL));
  return true;
}

bool add_mouse_device(struct sv filename) {
  struct vfs_mount *mount = vfs_find_mount(C_TO_SV("/dev"));
  assert(mount);
  assert(ramfs_add_file(mount, filename, mouse_open, NULL, NULL));
  return true;
}

void mouse_handler(struct cpu_status *r) {
  (void)r;
  bool has_data = false;
  switch (mouse_cycle) {
  case 0:
    mouse_u8[0] = inb(0x60);
    if (!(mouse_u8[0] & (1 << 3))) {
      mouse_cycle = 0;
      break;
    }
    mouse_cycle++;
    break;
  case 1:
    mouse_u8[1] = inb(0x60);
    mouse_cycle++;
    break;
  case 2: {
    mouse_u8[2] = inb(0x60);
    int16_t x = mouse_u8[1];
    int16_t y = mouse_u8[2];
    uint8_t xs = mouse_u8[0] & (1 << 4);
    uint8_t ys = mouse_u8[0] & (1 << 5);
    if (xs) {
      x |= 0xFF00;
    }
    if (ys) {
      y |= 0xFF00;
    }

    mouse_x += x;
    mouse_y -= y;

    if (mouse_x < 0) {
      mouse_x = 0;
    }
    if (mouse_y < 0) {
      mouse_y = 0;
    }

    mouse_middle_button = mouse_u8[0] & (1 << 2);
    mouse_right_button = mouse_u8[0] & (1 << 1);
    mouse_left_button = mouse_u8[0] & (1 << 0);

    mouse_cycle = 0;

    // If there is a change in the mouse buttons that event should be written.
    // The mouse movements will be generated upon request. This avoids
    // filling the ringbuffer with a ton of small mouse movements.

    bool button_change = false;
    if (mouse_middle_button != (previous_event.buttons & (1 << 2))) {
      button_change = true;
    } else if (mouse_right_button != (previous_event.buttons & (1 << 1))) {
      button_change = true;
    } else if (mouse_left_button != (previous_event.buttons & (1 << 0))) {
      button_change = true;
    }

    if (!button_change) {
      // A change has still occured, but there is not need to write to
      // the ringbuffer.
      has_data = true;
      break;
    }

    struct mouse_event e;
    e.buttons = mouse_u8[0];
    e.x = mouse_x;
    e.y = mouse_y;
    previous_event = e;
    ringbuffer_write(&mouse_buffer, (u8 *)&e, sizeof(e));
    has_data = true;
    break;
  }
  }
  for (u64 i = 0; i < listeners.length; i++) {
    struct vfs_fd *fd;
    assert(list_fd_get(&listeners, i, &fd));
    if (!fd) {
      continue;
    }
    vfs_notify_can_read(fd, has_data);
  }
  eoi(0xC);
}

bool ps2_mouse_init(void) {
  list_fd_init(&listeners);
  if (!ringbuffer_init(&mouse_buffer, 256)) {
    return false;
  }
  add_mouse_device(C_TO_SV("/dev/mouse"));
  handler_install(0x2C, mouse_handler, 0);
  // TODO: Why is this required?
  irq_clear_mask(0x2);
  return true;
}
