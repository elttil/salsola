ENTRY (_start)
OUTPUT_FORMAT("elf64-x86-64")

SECTIONS
{
	_kernel_start = .;

    . = 0x00008000;
    .trampoline.text : {
        *(.trampoline.text)
     }
    .trampoline.bss : {
        *(.trampoline.bss)
     }
    .trampoline.data : {
        *(.trampoline.data)
     }

    . = 0x00100000;

    .multiboot.data : {
         *(.multiboot.data)
     }
    .multiboot.bss : {
        *(.multiboot.bss)
     }
    .multiboot.text : {
        *(.multiboot.text)
     }

	
	KERNEL_VMA = 0xffffff8000000000;

	. += KERNEL_VMA;
	.text ALIGN (4K) : AT (ADDR (.text) - KERNEL_VMA)
	{
		*(.text .text.*)
	}
	.rodata ALIGN (4K) : AT (ADDR (.rodata) - KERNEL_VMA)
	{
		*(.rodata .rodata.*)
	}
	.data ALIGN (4K) : AT (ADDR (.data) - KERNEL_VMA)
	{
		*(.data .data.*)
	}
	.bss ALIGN (4K) : AT (ADDR (.bss) - KERNEL_VMA)
	{
		*(.bss .bss.bss.*)
		*(COMMON)
		*(.bootstrap_stack)
	}
	_kernel_end = .;
}
