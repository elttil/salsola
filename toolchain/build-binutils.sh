#!/bin/sh
#./download-binutils.sh


yes | rm -r build-binutils
yes | rm -r binutils-2.40


tar -xf binutils*.tar.xz
cd ./binutils-*/
patch -f -p1 -i ../binutils-2.40.diff

cd ld
AUTOCONF=`which autoconf2.69` automake-1.15
cd ..

cd ..
mkdir bin
PREFIX="$(pwd)/bin"
mkdir build-binutils
cd build-binutils
../binutils*/configure --target=x86_64-salsola --prefix="$PREFIX" --with-sysroot="$(pwd)/../../sysroot" --disable-werror
make -j$(nproc) && make install
