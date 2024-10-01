#!/bin/sh
# This installs dependencies for both DPDK and FastClick, support for apt-get(Debian, Ubuntu, ...) and apk (Alpine) for now. PRs are welcome.

opt=0
if [ "$1" = "--optional" ] ; then
    opt=1
fi

echo "Installing dependencies..." ;
if ( command -v apt-get &> /dev/null ) ; then
    echo "Using apt-get"

    apt-get update -yqq && apt-get install -yqq build-essential sudo wget libelf-dev pkg-config zlib1g-dev libnuma-dev python3-pyelftools ninja-build meson python3-pip libpcap-dev
    header=linux-headers-$(uname -r)
    if apt-cache search --names-only $header &> /dev/null ;  then
       apt-get install -yqq $header
    fi
    if [ $opt -eq 1 ] ; then
        echo "Installing optional dependencies"
        apt-get install -yqq libmicrohttpd-dev libhyperscan-dev libpci-dev libbpf-dev libpapi-dev libre2-dev llvm-dev
    fi
elif ( command -v apk &> /dev/null ) ; then
    echo "Using apk"
    echo "@testing https://dl-cdn.alpinelinux.org/alpine/edge/testing" >> /etc/apk/repositories
    apk add --no-cache  wget gcc libelf numactl python3 pkgconf zlib-dev py3-pip g++ py3-elftools autoconf build-base bsd-compat-headers linux-headers gcompat libstdc++ libpcap-dev libbpf-dev libxdp-dev
    apk add --no-cache rdma-core-dev@testing
fi

