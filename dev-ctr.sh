#!/bin/bash

clickdir=$(realpath `pwd`)

set -e

docker build $BUILD_ARGS -f builder.dock -t click-builder .

docker run \
  -v `pwd`:/src \
  -w /src \
  -h click🔨 \
  -it click-builder tmux -2
