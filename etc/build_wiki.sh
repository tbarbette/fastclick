#!/bin/bash
sudo ./deps.sh --optional
./configure CXXFLAGS="-std=gnu++11" --enable-user-multithread --disable-verbose-batch --enable-simtime --disable-clone --enable-dpdk --enable-all-elements --enable-flow --enable-batch --enable-ctx --enable-cpu-load --enable-rsspp --enable-flow-api --with-netmap=../netmap/sys/ --enable-user-timestamp
make -C doc install-man-markdown O=$(pwd)/../fastclick.wiki/elements
