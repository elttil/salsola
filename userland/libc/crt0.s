.global _start
.extern main
_start:
	movq 0(%rsp), %rdi
	mov 8(%rsp), %rsi
	call _libc_setup
