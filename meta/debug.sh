#!/bin/bash
scriptdir="$(dirname "$0")"
cd "$scriptdir"
cd ..

source meta/env.sh

#$QEMU -smp 4,sockets=1,cores=4,threads=1,maxcpus=4 -no-reboot -no-shutdown  -m 256M -cdrom $ISO_FILE \
#$QEMU -smp 2,sockets=1,cores=2,threads=1,maxcpus=2 -no-reboot -no-shutdown  -m 1G -cdrom $ISO_FILE \
$QEMU -enable-kvm -no-reboot -no-shutdown  -m 1G -cdrom $ISO_FILE \
    -display sdl \
	-audio driver=sdl,model=ac97,id=0 \
	-chardev stdio,id=char0,logfile=$LOG_FILE,signal=off \
	-serial chardev:char0 \
	-drive id=disk,file=$DISK_IMG,if=none \
	-netdev user,id=n0,hostfwd=tcp:127.0.0.1:6001-:80,hostfwd=tcp:127.0.0.1:6000-:20,hostfwd=tcp:127.0.0.1:6002-:443\
	-object filter-dump,maxlen=1024000,id=id,netdev=n0,file=./logs/netout\
	-device rtl8139,netdev=n0 \
	-device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 \
	-s -S &
#gdb -x ./.gdbinit
#gdb
gf2
