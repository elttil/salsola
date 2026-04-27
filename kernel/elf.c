#include <assert.h>
#include <crypto/SHA1/sha1.h>
#include <elf.h>
#include <fcntl.h>
#include <fs/vfs.h>
#include <mmu.h>
#include <prng.h>
#include <typedefs.h>

#include <kprintf.h>

err_t elf_parse(struct vfs_fd *fd) {
  err_t err;
  Elf64_Ehdr header;

  size_t rc;
  TRY_COND(vfs_pread(fd, &header, sizeof(header), 0, &rc), err, cleanup);
  if (sizeof(header) != rc) {
    err = ERROR_INVALID_ELF_HEADER;
    goto cleanup;
  }

  if (0 != memcmp(header.e_ident, "\x7F\x45\x4C\x46" /* "\x7FELF" */, 4)) {
    err = ERROR_INVALID_ELF_HEADER;
    goto cleanup;
  }
  return ERROR_SUCCESS;
cleanup:
  return err;
}

err_t elf_load_file(struct vfs_fd *fd, void **ds, void **entry) {
  err_t err;
  Elf64_Ehdr header;

  size_t rc;
  TRY_COND(vfs_pread(fd, &header, sizeof(header), 0, &rc), err, cleanup);
  if (sizeof(header) != rc) {
    err = ERROR_INVALID_ELF_HEADER;
    goto cleanup;
  }

  if (0 != memcmp(header.e_ident, "\x7F\x45\x4C\x46" /* "\x7FELF" */, 4)) {
    err = ERROR_INVALID_ELF_HEADER;
    goto cleanup;
  }

  Elf64_Phdr program_header;
  assert(sizeof(program_header) == header.e_phentsize);
  u64 header_offset = header.e_phoff;
  uintptr_t end_of_code = 0;
  for (int i = 0; i < header.e_phnum;
       i++, header_offset += header.e_phentsize) {

    TRY_COND(vfs_pread(fd, &program_header, sizeof(program_header),
                       header_offset, &rc),
             err, cleanup);
    if (0 >= rc) {
      err = ERROR_GENERIC_TODO;
      goto cleanup;
    }

    // FIXME: Only one type is supported, which is 1(load). More should be
    // added.
    assert(1 == program_header.p_type);

    // 1. Clear p_memsz bytes at p_vaddr to 0.(We also allocate frames for
    // that range)
    u64 p_memsz = program_header.p_memsz;
    u64 p_vaddr = program_header.p_vaddr;

    u64 pages_to_allocate =
        (u64)align_up_ptr((void *)(p_vaddr + p_memsz), PAGE_SIZE);
    pages_to_allocate -= p_vaddr - (p_vaddr % 0x1000);
    pages_to_allocate /= 0x1000;

    if (!mmu_allocate_region((void *)p_vaddr, pages_to_allocate * 0x1000,
                             MMU_FLAG_RW | MMU_FLAG_USER |
                                 MMU_FLAG_FAKE_ALLOCATION)) {
      err = ERROR_GENERIC_TODO;
      goto cleanup;
    }

    uintptr_t e = program_header.p_vaddr + program_header.p_memsz;
    if (e > end_of_code) {
      end_of_code = e;
    }

    // 2. Copy p_filesz bytes from p_offset to p_vaddr
    assert(ERROR_SUCCESS == vfs_pread(fd, (void *)program_header.p_vaddr,
                                      program_header.p_filesz,
                                      program_header.p_offset, &rc));

    assert(rc == program_header.p_filesz);
  }
  if (ds) {
    *ds = (void *)end_of_code;
  }
  ASSIGN_PTR(entry, (void *)header.e_entry);
  return ERROR_SUCCESS;

cleanup:
  return err;
}
