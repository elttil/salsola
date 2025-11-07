#include <stdlib.h>
#include <unistd.h>

#include <syscall.h>
#include <syscalls.h>

err_t sa_exec(struct sv file, struct sv *args, u32 number_of_args) {
  const char **strs = allocarray(sizeof(char *), number_of_args);
  u32 *lengths = allocarray(sizeof(u32), number_of_args);

  for (u32 i = 0; i < number_of_args; i++) {
    strs[i] = sv_buffer(args[i]);
    lengths[i] = sv_length(args[i]);
  }

  return syscall(SYS_EXEC, (u64)sv_buffer(file), sv_length(file), (u64)strs,
                 (u64)lengths, 0);
}
