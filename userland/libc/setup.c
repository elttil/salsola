#include <stddef.h>
#include <sys/random.h>

void _libc_setup() {
  sarandom_init(NULL);
  sarandom_set_aggressive_key_overwriting(NULL);
}
