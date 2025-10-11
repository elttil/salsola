#!/bin/bash
scriptdir="$(dirname "$0")"
cd "$scriptdir"
cd ..

source meta/env.sh

mkdir ./mount/ 2>/dev/null
sudo mount $DISK_IMG ./mount/
sudo cp -r ./sysroot/* ./mount/
sudo umount ./mount/
