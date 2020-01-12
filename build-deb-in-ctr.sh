#!/bin/bash

clickdir=$(realpath `pwd`)

set -e

docker build $BUILD_ARGS -f builder.dock -t click-builder .
docker run -v $clickdir:/click click-builder ./build-deb.sh
