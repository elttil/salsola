%define MAGIC         0xE85250D6
%define ARCH          0
%define HEADER_LENGTH 16
%define CHECKSUM -(MAGIC+ARCH+HEADER_LENGTH)

extern PML4T
extern PDPT

bits 32
section .multiboot.bss write nobits
	align 4096
PML4T:
	resb 4096
	resb 4096
PDPT:
	resb 4096
	resb 4096
PDT:
	resb 4096
	resb 4096
PT:
	resb 4096

section .multiboot
    align 16
section .multiboot.data
    align 4
    dd MAGIC
    dd ARCH
    dd HEADER_LENGTH
    dd CHECKSUM

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

GDT:
    .Null: equ $ - GDT
        dq 0
    .Code: equ $ - GDT
        dd 0xFFFF                                   ; Limit & Base (low, bits 0-15)
        db 0                                        ; Base (mid, bits 16-23)
        db PRESENT | NOT_SYS | EXEC | RW            ; Access
        db GRAN_4K | LONG_MODE | 0xF                ; Flags & Limit (high, bits 16-19)
        db 0                                        ; Base (high, bits 24-31)
    .Data: equ $ - GDT
        dd 0xFFFF                                   ; Limit & Base (low, bits 0-15)
        db 0                                        ; Base (mid, bits 16-23)
        db PRESENT | NOT_SYS | RW                   ; Access
        db GRAN_4K | SZ_32 | 0xF                    ; Flags & Limit (high, bits 16-19)
        db 0                                        ; Base (high, bits 24-31)
    .TSS: equ $ - GDT
        dd 0x00000068
        dd 0x00CF8900
    .Pointer:
        dw $ - GDT - 1
        dq GDT

global boot_page_directory
global boot_page_table1

global _start
extern _kernel_end


section .multiboot.text
_start:
	mov edx, eax

	; Disable paging
	mov eax, cr0
	and eax, 01111111111111111111111111111111b
	mov cr0, eax

	mov edi, PML4T
    mov cr3, edi
    xor eax, eax
    mov ecx, 4096
    rep stosd
    mov edi, cr3

	mov eax, PDPT
	or eax, 0x3
	mov DWORD [edi], eax ; PML4T[0]
	add edi, 4088
	mov DWORD [edi], eax ; PML4T[511]

    mov edi, PDPT
	mov eax, PDT
	or eax, 0x3
    mov DWORD [edi], eax

    mov edi, PDT
	mov eax, PT
	or eax, 0x3
    mov DWORD [edi], eax

    mov edi, PT
	mov eax, 0x00000003
    mov ecx, 512
    
.SetEntry:
    mov DWORD [edi], eax
    add eax, 0x1000
    add edi, 8
    loop .SetEntry

	; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

	mov edi, edx
	; Enter compat mode
	mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
	mov edx, edi

    ; Enable paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [GDT.Pointer]
    jmp GDT.Code:tmp_jump

bits 64

tmp_jump:
	mov rax, Realm64
	jmp rax

section .bootstrap_stack write nobits
stack_bottom:
resb 16384 ; 16 KiB
stack_top:

extern kmain

extern Realm64
section .text
Realm64:
	cli

	mov rsp, stack_top
	mov rbp, rsp

	push rbx
	push rdx

    mov ax, GDT.Data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax


	pop rdi
	pop rsi
	call kmain

;	mov rdi, 0xFFFFFF80000B8000
;
;	mov ebx, 0xFFFFFFFF
;    mov ecx, 512                 ; Set the C-register to 512.
;.Screen:
;    mov DWORD [rdi], ebx         ; Set the uint32_t at the destination index to the B-register.
;    add rdi, 2                   ; Add eight to the destination index.
;    loop .Screen               ; Set the next entry.

loop:
	hlt
	cli
	jmp loop
