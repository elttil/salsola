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

global cr2_get
cr2_get:
	mov rax, cr2
	ret

global flush_tss
flush_tss:
	mov ax, 40
	ltr ax
	ret

global swapgs
swapgs:
	swapgs
	ret

global rdrand
rdrand:
	cmp rdi, 0
	jz .invalid

	rdrand rax
	; If RDRAND did not return a random number let the caller retry.
	jnc .invalid

	mov [rdi], rax
	mov rax, 1
	ret

.invalid:
	mov rax, 0
	ret

global set_stack_and_jump
set_stack_and_jump:
	mov rsp, rdi
	mov rbp, rdi
	jmp rsi
