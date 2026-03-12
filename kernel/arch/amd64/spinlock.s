section .text

global lock_acquire
global lock_release
global lock_try

lock_try:
	;cli
    lock bts dword [rdi],0        ;Attempt to acquire the lock (in case lock is uncontended)
    jc .fail
	mov rax, 1
    ret
.fail:
	mov rax, 0
    ret

lock_acquire:
	;cli
    lock bts dword [rdi],0        ;Attempt to acquire the lock (in case lock is uncontended)
    jc .spin_with_pause
    ret

.spin_with_pause:
	;sti
    pause ; Tell CPU we're spinning
	;cli
    test dword [rdi],1 ; Is the lock free?
    jnz .spin_with_pause ; no, wait
    jmp lock_acquire ; retry

lock_release:
	;cli
    mov dword [rdi],0
    ret
