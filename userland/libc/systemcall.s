.global syscall
.global syscall_long
syscall_long:
	push %rbp
	mov %rsp, %rbp

	push %rbx
    push %r10
    push %r12
	push %r13

	and $0xFFFFFFFFFFFFFFF0, %rbp

	mov 16(%rbp), %r10
	mov 24(%rbp), %r12
	mov 32(%rbp), %r13

	mov %rcx, %rbx # Can't use rcx for syscall
	syscall

	pop %r13
	pop %r12
	pop %r10
	pop %rbx

	pop %rbp
	ret
syscall:
	push %rbp
	mov %rsp, %rbp

	push %rbx

	mov %rcx, %rbx # Can't use rcx for syscall
	syscall

	pop %rbx

	pop %rbp
	ret
