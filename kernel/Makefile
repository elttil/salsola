CC="x86_64-elf-gcc"
AS="x86_64-elf-as"
ARCH_OBJ=arch/amd64/boot.o arch/amd64/io.o arch/amd64/regs.o arch/amd64/mmu.o assert.o kmalloc.o crypto/ChaCha20/chacha20.o crypto/SHA1/sha1.o crypto/xoshiro256plusplus/xoshiro256plusplus.o csprng.o prng.o arch/amd64/idt.o arch/amd64/idt_asm.o drivers/ps2_keyboard.o ringbuffer.o drivers/pci.o drivers/ahci.o log.o arch/amd64/gdt.o task.o arch/amd64/task_switch.o
OBJ = $(ARCH_OBJ) kernel.o drivers/serial.o kprintf.o string.o
CFLAGS = -std=c2x -Os -mcmodel=large -ggdb -ffreestanding -Wall -Wextra -Werror -mgeneral-regs-only -mno-red-zone\
		 -Wno-int-to-pointer-cast \
		 -Wno-pointer-to-int-cast
ASMFLAGS= -g -felf64
LDFLAGS= -zmax-page-size=0x1000
INCLUDE= -I./arch/includes/ -I.
QEMU=~/prj/qemu-10.1.0/build/qemu-system-x86_64

all: myos.iso

%.o: %.c
	clang-format -i $<
	$(CC) $(INCLUDE) -c -o $@ $< $(CFLAGS)

%.o: %.s
	nasm $(ASMFLAGS) $< -o $@

myos.elf: $(OBJ)
	$(CC) $(INCLUDE) $(LDFLAGS) -shared -T linker.ld -o myos.elf -ffreestanding -nostdlib $(CFLAGS) $^ -lgcc

myos.iso: myos.elf
	cp myos.elf isodir/boot
	grub-mkrescue -o myos.iso isodir

run:
#	qemu-system-x86_64 -no-reboot -no-shutdown -d int -m 256M -cdrom ./myos.iso -chardev stdio,id=char0,logfile=./logs/serial.log,signal=off -serial chardev:char0 2>logs/error.log
	$(QEMU) -no-reboot -no-shutdown -d int -m 256M -cdrom ./myos.iso -chardev stdio,id=char0,logfile=./logs/serial.log,signal=off\
		-serial chardev:char0 \
		-drive id=disk,file=./disk.img,if=none \
		-device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0
	cat logs/serial.log

debug:
	qemu-system-x86_64 -no-reboot -no-shutdown -d int -m 256M -cdrom ./myos.iso -s -S \
		-chardev stdio,id=char0,logfile=./logs/serial.log,signal=off \
		-serial chardev:char0 \
		-drive id=disk,file=./disk.img,if=none \
		-device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 &
	gdb -x ../.gdbinit

clean:
	rm myos.elf myos.iso $(OBJ)
