.intel_syntax noprefix
.global outsw
.global outb
.global outw
.global outl
.global inb
.global inw
.global inl
.global rep_outsw
.global rep_insw

# ebx, esi, edi, ebp, and esp;
outsw:
    push ebp
	mov ebp, esp
	push esi
	mov dx, [ebp + 4+4]
	mov esi, [ebp + 8+4]
	outsw
	pop esi
	mov esp, ebp
	pop ebp
	ret

outl:
	mov eax, [esp + 8]
	mov dx, [esp + 4]
	out dx, eax
	ret

outb:
	mov al, [esp + 8]
	mov dx, [esp + 4]
	out dx, al
	ret

outw:
	mov ax, [esp + 8]
	mov dx, [esp + 4]
	out dx, ax
	ret

inl:
	mov dx, [esp + 4]
	in  eax, dx
	ret

inw:
	mov dx, [esp + 4]
	in  ax, dx
	ret

inb:
	mov dx, [esp + 4]
	in  al, dx
	ret

rep_outsw:
    push ebp
	mov ebp, esp
	push edi
	mov ecx, [ebp + 4+4]      #ECX is counter for OUTSW
	mov edx, [ebp + 8+4]      #Data port, in and out
	mov edi, [ebp + 12+4]		#Memory area
	rep outsw               #in to [RDI]
	pop edi
	mov esp, ebp
	pop ebp
	ret

rep_insw:
    push ebp
	mov ebp, esp
	push edi
	mov ecx, [ebp + 4+4]      #ECX is counter for INSW
	mov edx, [ebp + 8+4]      #Data port, in and out
	mov edi, [ebp + 12+4]		#Memory area
	rep insw                #in to [RDI]
	pop edi
	mov esp, ebp
	pop ebp
	ret
