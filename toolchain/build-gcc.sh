#!/bin/sh
#./download-gcc.sh
tar -xf gcc-*.tar.xz
cd ./gcc-*/
patch -f -p1 -i ../gcc-13.1.0.diff
cd ./libstdc++-v3/
autoconf2.69
cd ..
cd ..
mkdir bin
PREFIX=$(pwd)"/bin"
mkdir build-gcc
cd build-gcc
../gcc-*/configure --target=x86_64-salsola --prefix="$PREFIX" --with-gmp --with-mpfr --with-sysroot="$(pwd)/../../sysroot" --enable-languages=c,c++
make -j$(nproc) all-gcc all-target-libgcc && make install-gcc install-target-libgcc
