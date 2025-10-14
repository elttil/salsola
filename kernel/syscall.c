#include <arch/amd64/idt.h>
#include <kprintf.h>
#include <syscall.h>

struct syscall_arguments {
  uint64_t rbx;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t rbp;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rsp;
  uint64_t rax;
} __attribute__((packed));

void syscall_handler(struct syscall_arguments *args) {
  kprintf("Syscall rax: %x\n", args->rax);
  kprintf("Syscall rbx: %x\n", args->rbx);
  kprintf("Syscall rcx: %x\n", args->rcx);
  kprintf("Syscall rdx: %x\n", args->rdx);
  kprintf("Syscall rsp: %x\n", args->rsp);
  kprintf("Syscall r15: %x\n", args->r15);
  kprintf("Syscall r11: %x\n", args->r11);
}

void setup_syscall(void);
u64 set_kernel_stack(void *stack);

void syscall_init(void) {
  set_kernel_stack((void *)0xffffff8000000000 - 0x1000/*Guard page*/);
  setup_syscall();
}
