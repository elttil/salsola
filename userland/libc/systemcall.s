.global syscall
.global syscall_long
syscall_long:
syscall:
	push %r12
	push %rbx

	movq 32(%rsp), %r12 
	movq 24(%rsp), %r10

	mov %rcx, %rbx # Can't use rcx for syscall
	syscall

	pop %rbx
	pop %r12
	ret
