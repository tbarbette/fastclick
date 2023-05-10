#!/bin/sh

echo "Installing dependencies..." ;
if ( command -v apt-get &> /dev/null ) ; then
    echo "Using apt-get"        
    apt-get update -yqq && apt-get install -yqq build-essential sudo wget libelf-dev pkg-config zlib1g-dev libnuma-dev python3-pyelftools ninja-build meson linux-headers-$(uname -r) python3-pip
elif ( command -v apk &> /dev/null ) ; then
    echo "Using apk"
    apk add --no-cache  wget gcc libelf numactl python3 pkgconf zlib-dev py3-pip g++ py3-elftools autoconf build-base bsd-compat-headers linux-headers gcompat libstdc++
fi

