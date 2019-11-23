#!/bin/bash

export CFLAGS="-g -O3"
export CXXFLAGS="-g -O3 -std=gnu++11 -Wno-pmf-conversions -Wno-missing-field-initializers -Wno-pointer-arith -fpermissive"

./configure \
  --prefix=/usr/local \
  --enable-xdp \
  --enable-multithread \
  --disable-linuxmodule \
  --enable-intel-cpu \
  --disable-batch \
  --enable-user-multithread \
  --verbose \
  --disable-dynamic-linking \
  --enable-poll \
  --enable-bound-port-transfer \
  --disable-dpdk \
  --with-netmap=no \
  --enable-zerocopy \
  --disable-dpdk-packet \
  --enable-local \
  --enable-etherswitch
