#include <arch/includes/mmu.h>
#include <fs/ramfs.h>
#include <fs/tmpfs.h>

#include <kprintf.h>

// TODO: FIX A LOT OF STUFF

struct tmpfs_object {
  u8 *buffer;
  size_t length;
  size_t capacity;
  void *physical;
};

#define MIN_ALLOC_SIZE 256

err_t tmpfs_lseek(struct vfs_fd *fd, off_t offset, int whence, off_t *out) {
  struct tmpfs_object *object = fd->internal_object;
  off_t ret_offset = fd->offset;
  switch (whence) {
  case SEEK_SET:
    ret_offset = offset;
    break;
  case SEEK_CUR:
    ret_offset += offset;
    break;
  case SEEK_END:
    ret_offset = object->length;
    break;
  default:
    return ERROR_INVALID_WHENCE;
    break;
  }
  fd->offset = ret_offset;
  ASSIGN_PTR(out, ret_offset);
  return ERROR_SUCCESS;
}

err_t tmpfs_read(struct vfs_fd *fd, void *buffer, size_t length, size_t offset,
                 size_t *rc) {
  struct tmpfs_object *object = fd->internal_object;
  if (offset > object->length) {
    ASSIGN_PTR(rc, 0);
    return ERROR_SUCCESS;
  }
  length = min(length, object->length - offset);
  memcpy(buffer, object->buffer + offset, length);
  ASSIGN_PTR(rc, length);
  return ERROR_SUCCESS;
}

err_t tmpfs_write(struct vfs_fd *fd, const void *buffer, size_t length,
                  size_t offset, size_t *rc) {
  struct tmpfs_object *object = fd->internal_object;
  // FIXME: Check overflows
  if (length + offset > object->capacity) {
    size_t c = length + offset;
    c = max(c, object->capacity + MIN_ALLOC_SIZE);

    c = (size_t)align_up_ptr((void *)c, PAGE_SIZE);
    void *physical;
    void *r = kmalloc_align(c, &physical);
    if (!r) {
      return ERROR_NO_MEMORY;
    }
    memset(r, 0, c);
    kmalloc_align_free(object->buffer, object->capacity);
    object->buffer = r;
    object->capacity = c;
    object->physical = physical;
  }
  memcpy(object->buffer + offset, buffer, length);
  object->length = max(object->length, length + offset);
  ASSIGN_PTR(rc, length);
  return ERROR_SUCCESS;
}

err_t tmpfs_truncate(struct vfs_fd *fd, u64 length) {
  struct tmpfs_object *object = fd->internal_object;
  size_t c = length;
  c = (size_t)align_up_ptr((void *)c, PAGE_SIZE);
  void *physical;
  void *r = kmalloc_align(c, &physical);
  if (!r) {
    return ERROR_NO_MEMORY;
  }
  kmalloc_align_free(object->buffer, object->capacity);
  object->buffer = r;
  object->capacity = c;
  object->length = c;
  object->physical = physical;
  return ERROR_SUCCESS;
}

void tmpfs_close(struct vfs_fd *fd) {
  struct tmpfs_object *object = fd->internal_object;
  kmalloc_align_free(object->buffer, object->capacity);
  kfree(object);
  fd->internal_object = NULL;
}

err_t tmpfs_mmap(struct vfs_fd *fd, void *addr, size_t length, int prot,
                 int flags, size_t offset, void **out) {
  // TODO: Offset
  (void)offset;
  (void)flags;
  (void)prot;
  struct tmpfs_object *object = fd->internal_object;

  void *to_allocate = (void *)object->physical;
  size_t allocation_size = object->length;

  void *r;
  TRY(mmu_setup_random_region(addr, length, true, false, 0, &r));
  r = mmu_map_frames_to_region(to_allocate, allocation_size, r,
                               MMU_FLAG_RW | MMU_FLAG_USER | MMU_FLAG_SHARED);
  ASSIGN_PTR(out, r);
  return ERROR_SUCCESS;
}

bool tmpfs_open(struct vfs_fd *fd, struct sv file, int flags,
                void *internal_object, int *err) {
  (void)file;
  (void)flags;
  (void)internal_object;
  ASSIGN_PTR(err, ERROR_SUCCESS);
  struct tmpfs_object *object = kcalloc(1, sizeof(struct tmpfs_object));
  if (!object) {
    ASSIGN_PTR(err, ERROR_NO_MEMORY);
    return false;
  }

  fd->read = tmpfs_read;
  fd->write = tmpfs_write;
  fd->close = tmpfs_close;
  fd->lseek = tmpfs_lseek;
  fd->mmap = tmpfs_mmap;
  fd->truncate = tmpfs_truncate;
  fd->internal_object = object;
  fd->type = VFS_TYPE_BLOCK_DEVICE;

  return true;
}

bool tmpfs_add_device(struct sv filename) {
  struct vfs_mount *mount = vfs_find_mount(C_TO_SV("/dev"));
  if (!mount) {
    return false;
  }
  return ramfs_add_file(mount, filename, tmpfs_open, NULL, NULL);
}
