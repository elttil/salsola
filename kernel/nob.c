#define NOB_IMPLEMENTATION
#include "nob.h"

#define FLAG_IMPLEMENTATION
#include "./flag.h"

char *asm_files[] = {
    "arch/amd64/lock.s", "arch/amd64/smp_asm.s",     "arch/amd64/idt_asm.s",
    "arch/amd64/io.s",   "arch/amd64/boot.s",        "arch/amd64/msr.s",
    "arch/amd64/regs.s", "arch/amd64/task_switch.s", "arch/amd64/smap_asm.s",
};

char *c_files[] = {
    "buffer.c",
    "arch/amd64/smap.c",
    "arch/amd64/hwrng.c",
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
    "drivers/framebuffer.c",
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

bool release_build = false;
bool really_fast_build = false;
bool small_build = false;
bool ubsan = true;
bool debug = false;

/* Just appends -flto to LD and CC and removes stupid warnings */
bool uses_lto = false;

char *ld_flags[] = {
    "-shared",
    "-ffreestanding",
    "-nostdlib",
};

char *c_flags[] = {
    "-std=c99",
    "-mcmodel=large",
    "-ffreestanding",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-mgeneral-regs-only",
    "-mno-red-zone",
    "-Wno-int-to-pointer-cast",
    "-Wno-pointer-to-int-cast",
    "-I./arch/includes/",
    "-I.",
    "-I../include/",
    "-Werror=vla",
};

int format_files(Nob_File_Paths *files, Nob_Procs *procs) {
  if (0 == files->count) {
    return 1;
  }
  Nob_Cmd cmd = {0};
  nob_cmd_append(&cmd, "clang-format", "-i");
  nob_da_append_many(&cmd, files->items, files->count);
  if (!nob_cmd_run(&cmd, .async = procs)) {
    return 1;
  }
  return 0;
}

int build_c_file(char *file, char *object_output, Nob_Procs *procs,
                 int *did_rebuild) {
  int rebuild_is_needed = nob_needs_rebuild1(object_output, file);
  assert(rebuild_is_needed >= 0);
  if (!rebuild_is_needed) {
    if (did_rebuild) {
      *did_rebuild = 0;
    }
    return 0;
  }
  if (did_rebuild) {
    *did_rebuild = 1;
  }

  Nob_Cmd cmd = {0};
  nob_cmd_append(&cmd, CC, "-o", object_output, "-c", file);
  nob_da_append_many(&cmd, c_flags, ARRAY_LEN(c_flags));

  if (ubsan) {
    nob_cmd_append(&cmd, "-fsanitize=vla-bound,shift-exponent,pointer-overflow,"
                         "shift,signed-integer-overflow,bounds");
  }
  if (release_build) {
    nob_cmd_append(&cmd, "-O2");
  }

  if (debug) {
    nob_cmd_append(&cmd, "-ggdb");
  }
  if (small_build) {
    nob_cmd_append(&cmd, "-Oz");
  }
  if (really_fast_build) {
    nob_cmd_append(&cmd, "-O3");
  }
  if (uses_lto) {
    nob_cmd_append(&cmd, "-flto");
  }

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

void usage(const char *program, FILE *stream) {
  fprintf(stream, "Usage: %s [OPTIONS] [--] [ARGS]\n", program);
  fprintf(stream, "OPTIONS:\n");
  flag_print_options(stream);
}

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);

  bool flag_clean = false;
  bool flag_build = true;
  bool flag_build_override = false;

  flag_bool_var(&flag_clean, "clean", false, "Boolean flag");
  flag_bool_var(&flag_build_override, "build", false, "Boolean flag");
  flag_bool_var(&release_build, "release", false, "Boolean flag");
  flag_bool_var(&really_fast_build, "fast", false, "Boolean flag");
  flag_bool_var(&uses_lto, "lto", false, "Boolean flag");
  flag_bool_var(&debug, "debug", true, "Boolean flag");
  flag_bool_var(&small_build, "small", false, "Boolean flag");
  flag_bool_var(&ubsan, "ubsan", true, "Boolean flag");

  if (!flag_parse(argc, argv)) {
    usage(argv[0], stderr);
    flag_print_error(stderr);
    return 1;
  }

  argc = flag_rest_argc();
  argv = flag_rest_argv();

  if (flag_clean) {
    flag_build = false;
  }

  if (release_build) {
    ubsan = false;
  }

  if (really_fast_build) {
    ubsan = false;
    uses_lto = true;
  }

  if (small_build) {
    ubsan = false;
    uses_lto = true;
  }

  if (release_build || really_fast_build || uses_lto || small_build) {
    flag_build_override = true;
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
  if (uses_lto) {
    nob_cmd_append(&cmd, "-flto");
  }
  if (debug) {
    nob_cmd_append(&cmd, "-ggdb");
  }
  nob_da_append_many(&cmd, c_flags, ARRAY_LEN(c_flags));

  Nob_File_Paths objects = {0};
  Nob_File_Paths changed_files = {0};

  Nob_Procs procs = {0};
  for (int i = 0; i < ARRAY_LEN(c_files); i++) {
    char *object_output = code_file_to_obj(c_files[i]);
    int did_rebuild;
    assert(0 == build_c_file(c_files[i], object_output, &procs, &did_rebuild));
    nob_da_append(&objects, object_output);
    if (did_rebuild) {
      nob_da_append(&changed_files, c_files[i]);
    }
  }
  for (int i = 0; i < ARRAY_LEN(asm_files); i++) {
    char *object_output = code_file_to_obj(asm_files[i]);
    assert(0 == build_asm_file(asm_files[i], object_output, &procs));
    nob_da_append(&objects, object_output);
  }
  nob_procs_wait(procs);

  format_files(&changed_files, &procs);

  int rebuild_is_needed =
      nob_needs_rebuild(TARGET ".elf", objects.items, objects.count);
  if (rebuild_is_needed) {
    nob_da_append_many(&cmd, objects.items, objects.count);
    if (!nob_cmd_run(&cmd)) {
      return 1;
    }
  }
  nob_procs_wait(procs);

  if (!create_iso_file()) {
    return 1;
  }

  return 0;
}
