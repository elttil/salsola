#include <stdlib.h>
#include <unistd.h>

#include <syscall.h>
#include <syscalls.h>

struct sv *__getenv_array(size_t *length);
void __getenv_array_free(struct sv *env_array, size_t length);

err_t sa_exec(struct sv file, struct sv *args, u32 number_of_args) {
  size_t env_array_length;
  struct sv *env_array = __getenv_array(&env_array_length);
  if (!env_array) {
    return ERROR_NO_MEMORY;
  }

  err_t err = sa_exec_env(file, args, number_of_args, env_array, env_array_length);
  __getenv_array_free(env_array, env_array_length);
  return err;
}
