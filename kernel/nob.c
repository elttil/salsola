#define NOB_IMPLEMENTATION
#include "nob.h"

char *asm_files[] = {
    "arch/amd64/lock.s", "arch/amd64/smp_asm.s",     "arch/amd64/idt_asm.s",
    "arch/amd64/io.s",   "arch/amd64/boot.s",        "arch/amd64/msr.s",
    "arch/amd64/regs.s", "arch/amd64/task_switch.s",
};

char *c_files[] = {
    "arch/amd64/mmu.c",
    "assert.c",
    "kmalloc.c",
    "crypto/ChaCha20/chacha20.c",
    "crypto/SHA1/sha1.c",
    "crypto/xoshiro256plusplus/xoshiro256plusplus.c",
    "csprng.c",
    "prng.c",
    "arch/amd64/idt.c",
    "drivers/ps2_keyboard.c",
    "ringbuffer.c",
    "drivers/pci.c",
    "drivers/ahci.c",
    "log.c",
    "arch/amd64/gdt.c",
    "task.c",
    "drivers/pit.c",
    "sv.c",
    "ctype.c",
    "fs/vfs.c",
    "fs/ramfs.c",
    "arch/amd64/apic.c",
    "arch/amd64/smp.c",
    "fs/ext2.c",
    "elf.c",
    "ubsan.c",
    "syscall.c",
    "kernel.c",
    "drivers/serial.c",
    "kprintf.c",
    "string.c",
};

#define ARRAY_LEN(array) ((sizeof(array)) / (sizeof(array[0])))

#define CC "x86_64-salsola-gcc"
#define AS "nasm"

#define TARGET "salsola"

char *code_file_to_obj(char *file) {
  char *const obj = strdup(file);
  char *p = obj;
  for (; *p; p++)
    ;
  assert(p != obj);
  p--;
  assert('c' == *p || 's' == *p);
  *p = 'o';
  return obj;
}

char *ld_flags[] = {
    "-shared",
    "-ffreestanding",
    "-nostdlib",
};

char *c_flags[] = {
    "-std=c2x",
    "-O0",
    "-mcmodel=large",
    "-ggdb",
    "-ffreestanding",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-mgeneral-regs-only",
    "-mno-red-zone",
    "-Wno-int-to-pointer-cast",
    "-Wno-pointer-to-int-cast",
    "-fsanitize=vla-bound,shift-exponent,pointer-overflow,shift,signed-integer-"
    "overflow,bounds",
    "-I./arch/includes/",
    "-I.",
    "-I../include/",
};

int build_c_file(char *file, char *object_output, Nob_Procs *procs) {
  int rebuild_is_needed = nob_needs_rebuild1(object_output, file);
  assert(rebuild_is_needed >= 0);
  if (!rebuild_is_needed) {
    return 0;
  }
  Nob_Cmd cmd = {0};
  nob_cmd_append(&cmd, CC, "-o", object_output, "-c", file);
  nob_da_append_many(&cmd, c_flags, ARRAY_LEN(c_flags));
  if (!nob_cmd_run(&cmd, .async = procs)) {
    return 1;
  }
  return 0;
}

char *asm_flags[] = {
    "-g",
    "-felf64",
};

int build_asm_file(char *file, char *object_output, Nob_Procs *procs) {
  int rebuild_is_needed = nob_needs_rebuild1(object_output, file);
  assert(rebuild_is_needed >= 0);
  if (!rebuild_is_needed) {
    return 0;
  }

  Nob_Cmd cmd = {0};
  nob_cmd_append(&cmd, AS);
  nob_da_append_many(&cmd, asm_flags, ARRAY_LEN(asm_flags));
  nob_cmd_append(&cmd, "-o", object_output, file);
  if (!nob_cmd_run(&cmd, .async = procs)) {
    return 1;
  }
  return 0;
}

void rm_object(char *file) {
  char *object_output = code_file_to_obj(file);
  nob_delete_file(object_output);
  free(object_output);
}

int create_iso_file(void) {
  int rebuild_is_needed = nob_needs_rebuild1(TARGET ".iso", TARGET ".elf");
  if (!rebuild_is_needed) {
    return 1;
  }
  Nob_Cmd cmd = {0};
  nob_cmd_append(&cmd, "mv", TARGET ".elf", "isodir/boot");
  if (!nob_cmd_run(&cmd)) {
    return 0;
  }

  nob_cmd_append(&cmd, "grub-mkrescue", "-o", TARGET ".iso", "isodir");
  if (!nob_cmd_run(&cmd)) {
    return 0;
  }

  nob_cmd_append(&cmd, "ln", "isodir/boot/" TARGET ".elf", TARGET ".elf");
  if (!nob_cmd_run(&cmd)) {
    return 0;
  }
  return 1;
}

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);

  int flag_clean = 0;
  int flag_build = 1;
  int flag_build_override = 0;

  for (int i = 1; i < argc; i++) {
    if (0 == strcmp(argv[i], "clean")) {
      flag_clean = 1;
      flag_build = 0;
    }
    if (0 == strcmp(argv[i], "build")) {
      flag_build_override = 1;
    }
  }
  if (!flag_build) {
    flag_build = flag_build_override;
  }

  if (flag_clean) {
    for (int i = 0; i < ARRAY_LEN(c_files); i++) {
      rm_object(c_files[i]);
    }
    for (int i = 0; i < ARRAY_LEN(asm_files); i++) {
      rm_object(asm_files[i]);
    }
    nob_delete_file(TARGET ".elf");
    nob_delete_file(TARGET ".iso");
  }

  if (!flag_build) {
    return 0;
  }

  Nob_Cmd cmd = {0};
  nob_cmd_append(&cmd, CC, "-T", "linker.ld", "-o", TARGET ".elf");

  nob_da_append_many(&cmd, ld_flags, ARRAY_LEN(ld_flags));
  nob_da_append_many(&cmd, c_flags, ARRAY_LEN(c_flags));

  Nob_File_Paths objects = {0};

  Nob_Procs procs = {0};
  for (int i = 0; i < ARRAY_LEN(c_files); i++) {
    char *object_output = code_file_to_obj(c_files[i]);
    assert(0 == build_c_file(c_files[i], object_output, &procs));
    nob_da_append(&objects, object_output);
  }
  for (int i = 0; i < ARRAY_LEN(asm_files); i++) {
    char *object_output = code_file_to_obj(asm_files[i]);
    assert(0 == build_asm_file(asm_files[i], object_output, &procs));
    nob_da_append(&objects, object_output);
  }
  nob_procs_wait(procs);

  int rebuild_is_needed =
      nob_needs_rebuild(TARGET ".elf", objects.items, objects.count);
  if (rebuild_is_needed) {
    nob_da_append_many(&cmd, objects.items, objects.count);
    if (!nob_cmd_run(&cmd)) {
      return 1;
    }
  }

  if (!create_iso_file()) {
    return 1;
  }

  return 0;
}
