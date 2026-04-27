; preserve rbx, rsp, rbp, r12, r13, r14, and r15; 

section .text
global cpuid
cpuid:
    push rbp
	mov rbp, rsp
	push rbx ; CPUID modifies rbx

	mov rax, rdi
	cpuid

	mov [rsi+0], eax
	mov [rsi+4*1], ebx
	mov [rsi+4*2], ecx
	mov [rsi+4*3], edx

	pop rbx
    pop rbp
	ret

global msr_is_available
msr_is_available:
	push rbx ; CPUID modifies rbx

	mov rax, 1
	cpuid

	shr edx, 5
	and edx, 1
	mov eax, edx

	pop rbx ; CPUID modifies rbx
	ret

; u32 msr
; return value is the gotten value
global msr_get
msr_get:
	mov ecx, edi
	rdmsr
	shl rdx, 32
	or rax, rdx
	ret

global rdtsc
rdtsc:
	rdtsc
	shl rdx, 32
	or rax, rdx
	ret

; u32 msr
; u64 value
; return type is void
global msr_set
msr_set:
	mov ecx, edi

	mov eax, esi
	shr rsi, 32
	mov edx, esi

	wrmsr
	ret

global tsc_get_hz

; 1.193182 MHz
; So 0xFFFF is roughly 0.05492 seconds
; So take the result times 18 and you got your Hz
tsc_get_hz:
	cli
	; Disable the gate for channel 2 so the clock can be set.
	; This should only matter if the channel already has count
	mov rdx, 0x61
;	in al, 0x61
	in al, dx
	and al, 0xFE
	or al, 0x1
	out 0x61, al

	; Set mode
	mov al, 0b10110010
	out 0x43, al

	; 0x2e9b = 11931 which is close to the PIT Hz divided by 100
	mov al, 0x9b
	out 0x42, al
	mov al, 0x2e
	out 0x42, al

	rdtsc
	mov ecx, eax
	mov esi, edx

	; Set the gate for channel 2
	mov rdx, 0x61
	in al, dx

	or al, 0x1
	out 0x61, al

	; The fifth bit will(seems to) flip when the count is low.
	and al, 0x20
	jnz none_zero_check

zero_check:
	mov rdx, 0x61
	in al, dx

	and al, 0x20
	cmp al, 0
	jz zero_check
	jmp end

none_zero_check:
	mov rdx, 0x61
	in al, 0x61

	and al, 0x20
	cmp al, 0
	jnz none_zero_check
end:
	rdtsc

	sub eax, ecx
	sub edx, esi
	shl rdx, 32
	or rax, rdx
	ret
