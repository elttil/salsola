global outsw
global outb
global outw
global outl
global inb
global inw
global inl

; args:       rdi, rsi, rdx, rcx, r8, r9
; preserve:   rbx, rsp, rbp, r12, r13, r14, r15
; may modify: rax, rdi, rsi, rdx, rcx, r8, r9, r10, r11
outsw:
	mov dx, di
	outsw
	ret

outl:
	mov dx, di
	mov eax, esi
	out dx, eax
	ret

outb:
	mov dx, di
	mov ax, si
	out dx, al
	ret

outw:
	mov dx, di
	mov ax, si
	out dx, ax
	ret

inl:
	movzx rdx, di
	in eax, dx
	ret

inw:
	movzx rdx, di
	mov eax, 0
	in ax, dx
	ret

inb:
	movzx rdx, di
	mov eax, 0
	in al, dx
	ret

global set_stack_and_jump
set_stack_and_jump:
	mov rsp, rdi
	mov rbp, rdi
	jmp rsi
