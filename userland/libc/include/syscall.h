#ifndef SYSCALL_H
#define SYSCALL_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <typedefs.h>
u64 syscall_long(u64 rdi, u64 rsi, u64 rdx, u64 rcx, u64 r8, u64 r9, u64 r10,
            u64 r11, u64 r12);
u64 syscall(u64 rdi, u64 rsi, u64 rdx, u64 rcx, u64 r8, u64 r9);
#endif
