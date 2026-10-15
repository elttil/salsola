#!/bin/bash
scriptdir="$(dirname "$0")"
cd "$scriptdir"
cd ..

source meta/env.sh

cd kernel/
./nob -clean
#./nob &
#NOB_BUILD=$!
./nob || exit 1
cd ..

#$MAKE_CMD clean -C ./userland/libc/
$MAKE_CMD -C ./userland/libc/ || exit 1
$MAKE_CMD -C ./userland/libc/ install || exit 1

#$MAKE_CMD clean -C ./userland/init/
$MAKE_CMD -C ./userland/init/ || exit 1
$MAKE_CMD -C ./userland/init/ install || exit 1

$MAKE_CMD clean -C ./userland/minibox/
$MAKE_CMD -C ./userland/minibox/
$MAKE_CMD -C ./userland/minibox/ install
#$MAKE_CMD -C ./userland/init/ install

#$MAKE_CMD clean -C ./userland/font/
$MAKE_CMD -C ./userland/font/ || exit 1
$MAKE_CMD -C ./userland/font/ install || exit 1

$MAKE_CMD -C ./userland/minibox/ || exit 1
$MAKE_CMD -C ./userland/minibox/ install || exit 1

$MAKE_CMD -C ./userland/wmx/ || exit 1
$MAKE_CMD -C ./userland/wmx/ install || exit 1

$MAKE_CMD -C ./userland/libdriver/ || exit 1
$MAKE_CMD -C ./userland/libdriver/ install || exit 1

$MAKE_CMD -C ./userland/libgui/ || exit 1
$MAKE_CMD -C ./userland/libgui/ install || exit 1

$MAKE_CMD -C ./userland/ac97/ || exit 1
$MAKE_CMD -C ./userland/ac97/ install || exit 1

$MAKE_CMD -C ./userland/rtl8139/ || exit 1
$MAKE_CMD -C ./userland/rtl8139/ install || exit 1

$MAKE_CMD -C ./userland/gl/ || exit 1
$MAKE_CMD -C ./userland/gl/ install || exit 1

$MAKE_CMD -C ./userland/test/ || exit 1
$MAKE_CMD -C ./userland/test/ install || exit 1

$MAKE_CMD -C ./userland/httpd/ || exit 1
$MAKE_CMD -C ./userland/httpd/ install || exit 1

$MAKE_CMD -C ./userland/net/ || exit 1
$MAKE_CMD -C ./userland/net/ install || exit 1

$MAKE_CMD -C ./userland/graph/ || exit 1
$MAKE_CMD -C ./userland/graph/ install || exit 1

$MAKE_CMD -C ./userland/doomgeneric/doomgeneric || exit 1
cp ./userland/doomgeneric/doomgeneric/doomgeneric ./sysroot/bin/
cp ~/prj/sbos/sysroot/doom1.wad ./sysroot/doom1.wad
