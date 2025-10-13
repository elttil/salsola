#!/bin/sh
#wget "https://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.gz"
tar -xvf autoconf-2.69.tar.gz
cd autoconf-2.69
./configure
make -j`nproc`
sudo make install
