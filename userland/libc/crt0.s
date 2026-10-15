.global _start
.extern main
_start:
	movq 0(%rsp), %rdi
	mov 8(%rsp), %rsi
	mov 16(%rsp), %rdx
	call _libc_setup
