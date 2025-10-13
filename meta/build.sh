#!/bin/bash
scriptdir="$(dirname "$0")"
cd "$scriptdir"
cd ..

source meta/env.sh

cd kernel/
./nob
cd ..

#$MAKE_CMD -C ./userland/libc/
#$MAKE_CMD -C ./userland/libc/ install
#
#$MAKE_CMD -C ./userland/init/
#$MAKE_CMD -C ./userland/init/ install
