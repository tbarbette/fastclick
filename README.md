# FastClick (PacketMill)

This repo is a modified FastClick that comes with some additional source-code optimization techniques to improve the performance of high-speed packet processing. Additionally, it implements different metadata management models, i.e., copying, overlaying, and X-Change.

For more information, please refer to PacketMill's [paper][packetmill-paper] and [repo][packetmill-repo].


## Source-code Modifications

PacketMill performs multiple source-code optimizations that exploit the availabe information in a given NF configuration file. We have implemented these optimizations on top of `click-devirtualize`. 

To use these optimization, you need to build and install FastClick as follows:


```bash
git clone --branch packetmill git@github.com:tbarbette/fastclick.git
cd fastclick
./configure --disable-linuxmodule --enable-userlevel --enable-user-multithread --enable-etherswitch --disable-dynamic-linking --enable-local --enable-dpdk --enable-research --enable-gtp --disable-task-stats --enable-flow --disable-task-stats --enable-cpu-load --prefix $(pwd)/build/ --enable-intel-cpu CXX="clang++ -fno-access-control" CC="clang" CXXFLAGS="-std=gnu++14 -O3" --disable-bound-port-transfer --enable-dpdk-pool --disable-dpdk-packet
make
sudo make uninstall
sudo make install
```

You need to define `RTE_SDK` and `RTE_TARGET` before configuring FastClick.

**Note: PacketMill's [repo][packetmill-repo] offers a automated pipeline/workflow for building/installing PacketMill (FastClick + X-Change) and performing some experiments related to PacketMill's PacketMill's [paper][packetmill-paper].**

### Devirtualize Pass

### Replace Pass

### Static Pass

```bash
export CLICK_ELEM_RAND_SEED=$RAND_SEED
export CLICK_ELEM_RAND_MAX=$RAND_MAX
```

### Inline Pass

## X-Change

This modified version of FastClick also support `X-Change` API used by PacketMill. For more information, please refer to PacketMill's [repo][packetmill-repo] and X-Change [repo][x-change-repo].


[packetmill-paper]: https://people.kth.se/~farshin/documents/packetmill-asplos21.pdf
[packetmill-repo]: https://github.com/aliireza/packetmill 
[x-change-repo]: https://github.com/tbarbette/xchange
