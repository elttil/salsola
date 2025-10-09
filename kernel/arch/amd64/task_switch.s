section .text
global switch_to_task
global weird_switch

extern task_create_directory

weird_switch:
	call weird_switch2
	mov rax, [rdi+24]
;	mov rax, 0x1
	ret
weird_return:
	mov rax, 0x0
	ret

weird_switch2:
	push rdi
	call task_create_directory
	pop rdi

	mov rax, [rdi+8]; cr3

	mov r8, cr3
	mov cr3, rax

		mov rax, weird_return
		mov [rsp], rax

		push rbx
		push rbp
		push r12
		push r13
		push r14
		push r15

		mov [rdi], rsp

		add rsp, 8*6

	mov cr3, r8
	ret

; preserve: rbx, rsp, rbp, r12, r13, r14, and r15
; args: rdi, rsi
switch_to_task:
	push rbx
	push rbp
	push r12
	push r13
	push r14
	push r15

	; Preserve current state
	mov [rdi], rsp ; rsp
	mov rax, cr3
	mov [rdi+8], rax ; cr3
;	mov [rdi+16], rsp0 ; rsp0

	mov rsp, [rsi] ; rsp
	mov rax, [rsi+8] ; cr3
	mov rbx, [rsi+16] ; rsp0

    mov rcx,cr3
 
    cmp rax,rcx

    je .doneVAS
    mov cr3,rax
.doneVAS:
 
	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rbx
 
    sti
    ret

global jump_usermode
jump_usermode:
	mov ax, (4 * 8) | 3 ; ring 3 data with bottom 2 bits set for ring 3
	mov ds, ax
	mov es, ax 
	mov fs, ax 
	mov gs, ax ; SS is handled by iret

	; set up the stack frame iret expects
	push (4 * 8) | 3 ; data selector
	push rsi ; current esp
	pushf ; eflags
	push (3 * 8) | 3 ; code selector (ring 3 code with bottom 2 bits set for ring 3)
	push rdi ; instruction address to return to
	iretq


jump_usermode2:
	mov ax, (4 * 8) | 3 ; user data segment with RPL 3
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax ; sysexit sets SS

	; setup wrmsr inputs
	xor rdx, rdx ; not necessary; set to 0
	mov rax, 0x8 ; the segments are computed as follows: CS=MSR+0x10 (0x8+0x10=0x18), SS=MSR+0x18 (0x8+0x18=0x20).
	mov rcx, 0x174 ; MSR specifier: IA32_SYSENTER_CS
	wrmsr ; set sysexit segments

	; setup sysexit inputs
	mov rdx, rdi ; to be loaded into EIP
;	mov rcx, rsp ; to be loaded into ESP
	mov rcx, rsi ; to be loaded into ESP
	sysexit
