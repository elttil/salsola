section .text

global lock_acquire
global lock_release
lock_acquire:
    lock bts dword [rdi],0        ;Attempt to acquire the lock (in case lock is uncontended)
    jc .spin_with_pause
    ret

.spin_with_pause:
    pause ; Tell CPU we're spinning
    test dword [rdi],1 ; Is the lock free?
    jnz .spin_with_pause ; no, wait
    jmp lock_acquire ; retry

lock_release:
    mov dword [rdi],0
    ret
