section .text

global wait_for_delivery
wait_for_delivery:
.loop:
	pause
	cmp DWORD [rdi], (1<<12)
	jz .loop
	ret

section .trampoline.bss
global trampoline_gdt
align 64
trampoline_gdt:
	resb 8
global trampoline_cr3
trampoline_cr3:
	resb 4

section .trampoline.data
; Access bits
PRESENT        equ 1 << 7
NOT_SYS        equ 1 << 4
EXEC           equ 1 << 3
DC             equ 1 << 2
RW             equ 1 << 1
ACCESSED       equ 1 << 0

; Flags bits
GRAN_4K       equ 1 << 7
SZ_32         equ 1 << 6
LONG_MODE     equ 1 << 5

GDT2:
    .Null: equ $ - GDT2
        dq 0
    .Code: equ $ - GDT2
        dd 0xFFFF                                   ; Limit & Base (low, bits 0-15)
        db 0                                        ; Base (mid, bits 16-23)
        db PRESENT | NOT_SYS | EXEC | RW            ; Access
        db GRAN_4K | LONG_MODE | 0xF                ; Flags & Limit (high, bits 16-19)
        db 0                                        ; Base (high, bits 24-31)
    .Data: equ $ - GDT2
        dd 0xFFFF                                   ; Limit & Base (low, bits 0-15)
        db 0                                        ; Base (mid, bits 16-23)
        db PRESENT | NOT_SYS | RW                   ; Access
        db GRAN_4K | SZ_32 | 0xF                    ; Flags & Limit (high, bits 16-19)
        db 0                                        ; Base (high, bits 24-31)
    .TSS: equ $ - GDT2
        dd 0x00000068
        dd 0x00CF8900
    .Pointer:
        dw $ - GDT2 - 1
        dq GDT2

section .trampoline.text

extern ap_startup

bits 16
align 16 ; TODO: Is this necessary?
ap_tramp2:
	cli
	cld
	jmp 0:ap_stage2
ap_stage2:
	; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax


	mov eax, trampoline_cr3
	mov eax, [eax]
	mov cr3, eax

	mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

	; Enable long mode
	mov ebx, 0x80000011
	mov cr0, ebx

    lgdt [GDT2.Pointer]
    jmp GDT2.Code:ap_stage3


align 64
bits 64
ap_stage3:
	mov ax, GDT2.Data
	mov ds, ax
	mov ss, ax
	mov rsp, stack_top
	mov rbp, stack_top
	mov rax, ap_startup
	jmp rax

section .trampoline.bss
stack_bottom:
	resb 4096
	resb 4096
stack_top:
