; preserve rbx, rsp, rbp, r12, r13, r14, and r15; 

section .text
global cpuid
cpuid:
	push rbx ; CPUID modifies rbx

	mov rax, rdi
	cpuid

	mov [rsi+0], eax
	mov [rsi+4*1], ebx
	mov [rsi+4*2], ecx
	mov [rsi+4*3], edx

	pop rbx
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
