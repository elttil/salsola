.global _start
.extern main
_start:
	call _libc_setup
	movq 0(%rsp), %rdi
	mov 8(%rsp), %rsi
	call main
# TODO: Call exit
#	mov %eax, %ebx
#	mov $8, %eax
#	int $0x80
l:
	nop
	nop
	nop
	nop
	jmp l
