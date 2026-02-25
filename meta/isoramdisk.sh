#!/bin/sh
scriptdir="$(dirname "$0")"
cd "$scriptdir"
cd ..

source meta/env.sh

export RAMDISK_PATH=./kernel/isodir_ramdisk/boot/ramdisk.img
rm $RAMDISK_PATH
touch $RAMDISK_PATH
truncate -s 8M $RAMDISK_PATH
mkfs.ext2 $RAMDISK_PATH

mkdir ./mount/ 2>/dev/null
sudo mount $RAMDISK_PATH ./mount/ || exit 1

sudo cp -r ./sysroot/bin/ ./mount/
sudo cp -r ./sysroot/script.sh ./mount/
sudo cp -r ./sysroot/init.sh ./mount/
sudo cp -r ./sysroot/font.ttf ./mount/

#yes | rm -r ./mount/lib/gcc/

sudo umount ./mount/

e2fsck -f $RAMDISK_PATH
resize2fs $RAMDISK_PATH

cd kernel/
./nob -clean
./nob -ramdisk || exit 1
cd ..
