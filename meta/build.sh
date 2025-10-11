#!/bin/bash
scriptdir="$(dirname "$0")"
cd "$scriptdir"
cd ..

source meta/env.sh

$MAKE_CMD -C ./kernel/

$MAKE_CMD -C ./userland/libc/
$MAKE_CMD -C ./userland/libc/ install

$MAKE_CMD -C ./userland/init/
$MAKE_CMD -C ./userland/init/ install
