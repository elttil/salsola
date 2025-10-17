global asm_smap_enable
asm_smap_enable:
	mov rax, cr4
	or rax, (1 << 21)
	mov cr4, rax
	ret

global asm_smap_disable
asm_smap_disable:
	mov rax, cr4
	and rax, ~(1 << 21)
	mov cr4, rax
	ret
