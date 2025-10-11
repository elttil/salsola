#!/bin/sh
QEMU=~/prj/qemu-10.1.0/build/qemu-system-x86_64
#QEMU=qemu-system-x86_64

DISK_IMG=./meta/disk.img
LOG_FILE=./logs/serial.log
ISO_FILE=./kernel/myos.iso

NPROC=`nproc`

MAKE_CMD="make -j$NPROC"
