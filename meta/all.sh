#!/bin/bash
scriptdir="$(dirname "$0")"
cd "$scriptdir"
cd ..

source meta/env.sh

./meta/build.sh
./meta/install.sh
./meta/run.sh
