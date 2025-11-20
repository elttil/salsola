#include <arch/amd64/msr.h>
#include <fs/ramfs.h>
#include <fs/vfs.h>
#include <kprintf.h>
#include <sb.h>
#include <timer.h>

u64 tsc_mhz;
u64 tsc_start;

void timer_init(void) {
  tsc_mhz = tsc_get_hz() / 10000;
  tsc_start = rdtsc();
}

u64 timer_get_ms(void) {
  return (rdtsc() - tsc_start) / (tsc_mhz * 1000);
}

err_t timer_read(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                 size_t *rc) {
  (void)fd;
  (void)offset;

  struct sb ctx;
  sb_init_buffer(&ctx, buffer, length);
  sb_set_ignore(&ctx, offset);

  u64 r = timer_get_ms();
  (void)ksbprintf(&ctx, "%llu", r);
  ASSIGN_PTR(rc, sv_length(SB_TO_SV(ctx)));
  return ERROR_SUCCESS;
}

bool timer_open(struct vfs_fd *fd, struct sv file, int flags,
                void *internal_object, int *err) {
  (void)fd;
  (void)file;
  (void)flags;
  (void)internal_object;
  (void)err;
  fd->type = VFS_TYPE_CHAR_DEVICE;
  fd->read = timer_read;
  //  fd->write = timer_write;
  return true;
}

bool timer_add_device(struct sv filename) {
  struct vfs_mount *mount = vfs_find_mount(C_TO_SV("/dev"));
  if (!mount) {
    return false;
  }
  return ramfs_add_file(mount, filename, timer_open, NULL, NULL);
}
