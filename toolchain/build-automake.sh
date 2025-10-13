#!/bin/sh
wget "https://ftp.gnu.org/gnu/automake/automake-1.15.1.tar.xz"
tar -xvf automake-1.15.1.tar.xz
cd automake-1.15.1
./configure
make -j`nproc`
sudo make install
