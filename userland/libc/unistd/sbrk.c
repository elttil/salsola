#include <stdio.h>
#include <syscall.h>
#include <syscalls.h>
#include <unistd.h>

void *sbrk(intptr_t increment) {
  printf("TODO: SBRK is deprecated, DONT USE\n");
  (void)increment;
  return NULL;
  //  return (void *)syscall(SYS_SBRK, increment, 0, 0, 0, 0);
}
