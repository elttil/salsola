#!/bin/bash
scriptdir="$(dirname "$0")"
cd "$scriptdir"
cd ..

source meta/env.sh

#$QEMU -smp 2,sockets=1,cores=2,threads=1,maxcpus=2 -no-reboot -no-shutdown -m 256M -cdrom $ISO_FILE\
#$QEMU -d int -smp 2,sockets=1,cores=2,threads=1,maxcpus=2 -no-reboot -no-shutdown -m 256M -cdrom $ISO_FILE\
#$QEMU -smp 2,sockets=1,cores=2,threads=1,maxcpus=2 -no-reboot -no-shutdown -m 1G -cdrom $ISO_FILE\
#$QEMU -no-reboot -no-shutdown -m 2G -cdrom $ISO_FILE\
#$QEMU -smp 2,sockets=1,cores=2,threads=1,maxcpus=2 -no-reboot -no-shutdown -m 2G -cdrom $ISO_FILE\
#$QEMU -no-reboot -no-shutdown -m 128M -cdrom $ISO_FILE\
#$QEMU -no-reboot -no-shutdown -m 128M -cdrom $ISO_FILE\
#$QEMU -enable-kvm -no-reboot -no-shutdown -m 128M -cdrom $ISO_FILE\
#$QEMU -enable-kvm -smp 2,sockets=1,cores=2,threads=1,maxcpus=2 -no-reboot -no-shutdown -m 256M -cdrom $ISO_FILE\
#$QEMU -no-reboot -no-shutdown -m 256M -cdrom $ISO_FILE\
#$QEMU -enable-kvm -machine kernel_irqchip=on -no-reboot -no-shutdown -m 256M -cdrom $ISO_FILE\
#$QEMU -enable-kvm -machine kernel_irqchip=on -no-reboot -no-shutdown -m 1G -cdrom $ISO_FILE\
#$QEMU -enable-kvm -machine kernel_irqchip=on -smp 2,sockets=1,cores=2,threads=1,maxcpus=2 -no-reboot -no-shutdown -m 512M -cdrom $ISO_FILE\
#$QEMU -enable-kvm -machine kernel_irqchip=on -smp 2,sockets=1,cores=2,threads=1,maxcpus=2 -no-reboot -no-shutdown -m 48M -cdrom $ISO_FILE\
$QEMU -enable-kvm -machine kernel_irqchip=on -no-reboot -no-shutdown -m 128M -cdrom $ISO_FILE\
	-chardev stdio,id=char0,logfile=$LOG_FILE,signal=off \
	-serial chardev:char0 \
	-drive id=disk,file=$DISK_IMG,if=none \
	-device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 \
	-audio driver=sdl,model=ac97,id=0 \
	-netdev user,id=n0,hostfwd=tcp:127.0.0.1:6001-:80,hostfwd=tcp:127.0.0.1:6000-:20,hostfwd=tcp:127.0.0.1:6002-:443\
	-object filter-dump,maxlen=1024000,id=id,netdev=n0,file=./logs/netout \
	-device rtl8139,netdev=n0 -s
