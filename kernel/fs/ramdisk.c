#include <crypto/SHA1/sha1.h>
#include <fs/ramdisk.h>

#include <kprintf.h>

struct ramdisk {
  void *address;
  u64 size;
};

err_t ramdisk_write(struct vfs_fd *fd, const void *buffer, size_t length,
                   size_t offset, size_t *rc) {
  struct ramdisk *ramdisk = fd->internal_object;

  if (offset > ramdisk->size) {
    ASSIGN_PTR(rc, 0);
    return ERROR_SUCCESS;
  }

  if (length > ramdisk->size - offset) {
    length = ramdisk->size - offset;
    kprintf("OUT OF BOUNDS\n");
  }

  memcpy((u8 *)ramdisk->address + offset, buffer, length);
  ASSIGN_PTR(rc, length);
  return ERROR_SUCCESS;
}

err_t ramdisk_read(struct vfs_fd *fd, void *buffer, size_t length,
                   size_t offset, size_t *rc) {
  struct ramdisk *ramdisk = fd->internal_object;

  if (offset > ramdisk->size) {
    ASSIGN_PTR(rc, 0);
    return ERROR_SUCCESS;
  }

  if (length > ramdisk->size - offset) {
    length = ramdisk->size - offset;
    kprintf("OUT OF BOUNDS\n");
  }

  memcpy(buffer, (u8 *)ramdisk->address + offset, length);
  ASSIGN_PTR(rc, length);
  return ERROR_SUCCESS;
}

struct vfs_fd *ramdisk_create(void *address, u64 size) {
  struct ramdisk *ramdisk = kmalloc(sizeof(struct ramdisk));
  if (!ramdisk) {
    return NULL;
  }
  struct vfs_fd *fd = vfs_allocate_fd();
  if (!fd) {
    kfree(ramdisk);
    return NULL;
  }
  ramdisk->address = address;
  ramdisk->size = size;
  fd->read = ramdisk_read;
  fd->write = ramdisk_write;
  fd->internal_object = ramdisk;
  fd->type = VFS_TYPE_BLOCK_DEVICE;

  return fd;
}
