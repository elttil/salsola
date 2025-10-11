#!/bin/bash
scriptdir="$(dirname "$0")"
cd "$scriptdir"
cd ..

source meta/env.sh

#$QEMU -d int -cpu core2duo-v1 -smp 2,sockets=1,cores=2,threads=1,maxcpus=2 -no-reboot -no-shutdown -m 256M -cdrom $ISO_FILE\
$QEMU -d int -smp 2,sockets=1,cores=2,threads=1,maxcpus=2 -no-reboot -no-shutdown -m 256M -cdrom $ISO_FILE\
	-chardev stdio,id=char0,logfile=$LOG_FILE,signal=off\
	-serial chardev:char0 \
	-drive id=disk,file=$DISK_IMG,if=none \
	-device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0
