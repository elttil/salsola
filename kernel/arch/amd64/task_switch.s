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
		push r12
		push r13
		push r14
		push r15

		mov [rdi], rsp

		add rsp, 8*5

	mov cr3, r8
	ret

; preserve: rbx, rsp, rbp, r12, r13, r14, and r15
; args: rdi, rsi
switch_to_task:
	push rbx
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
	pop rbx
 
    sti
    ret
