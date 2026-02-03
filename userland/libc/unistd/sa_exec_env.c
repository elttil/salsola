#include <stdlib.h>
#include <unistd.h>

#include <syscall.h>
#include <syscalls.h>

err_t sa_exec_env(struct sv file, struct sv *args, u32 number_of_args,
                  struct sv *envs, u32 number_of_envs) {
  const char **strs = allocarray(sizeof(char *), number_of_args);
  u32 *arg_lengths = allocarray(sizeof(u32), number_of_args);

  for (u32 i = 0; i < number_of_args; i++) {
    strs[i] = sv_buffer(args[i]);
    arg_lengths[i] = sv_length(args[i]);
  }

  const char **envs_array = allocarray(sizeof(char *), number_of_envs);
  u32 *env_lengths = allocarray(sizeof(u32), number_of_envs);

  for (u32 i = 0; i < number_of_envs; i++) {
    envs_array[i] = sv_buffer(envs[i]);
    env_lengths[i] = sv_length(envs[i]);
  }

  return syscall_long(SYS_EXEC, (u64)sv_buffer(file), sv_length(file),
                      (u64)strs, (u64)arg_lengths, number_of_args,
                      (u64)envs_array, (u64)env_lengths, number_of_envs);
}
