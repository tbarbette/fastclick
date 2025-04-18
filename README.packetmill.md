# PacketMill

PacketMill modified FastClick with some additional source-code optimization techniques to improve the performance of high-speed packet processing. Additionally, it implements different metadata management models, i.e., copying, overlaying, and X-Change.

For more information, please refer to PacketMill's [paper][packetmill-paper] and [repo][packetmill-repo].

## Building 
PacketMill performs multiple source-code optimizations that exploit the available information in a given NF configuration file. We have implemented these optimizations on top of `click-devirtualize`.

To use these optimizations, you need to build and install FastClick as follows:

```bash
git clone git@github.com:tbarbette/fastclick.git
cd fastclick
./configure --disable-linuxmodule --enable-userlevel --enable-user-multithread --enable-etherswitch --disable-dynamic-linking --enable-local --enable-dpdk --enable-research --enable-flow --disable-task-stats --enable-cpu-load --prefix $(pwd)/build/ --enable-intel-cpu CXX="clang++ -fno-access-control" CC="clang" CXXFLAGS="-std=gnu++14 -O3" --disable-bound-port-transfer --enable-dpdk-pool --disable-dpdk-packet
make
sudo make uninstall
sudo make install
```

You need to define `RTE_SDK` and `RTE_TARGET` before configuring FastClick.

**Note: PacketMill's [repo][packetmill-repo] offers an automated pipeline/workflow for building/installing PacketMill (FastClick + X-Change) and performing some experiments related to PacketMill's PacketMill's [paper][packetmill-paper].** This guide is only about the FastClick part, X-Change must be compiled separately.

Here is an overview of the configuration summary (without X-Change). DPDK and LLVM should appear as yes:
```
Configuration Summary:
    C     Compiler:              clang
    C++   Compiler:              clang++
    CFLAGS:                      -march=native -g -O2 -W -Wall -mavx2 -msse2 -msse4.2
    CXXFLAGS:                    -march=native -std=gnu++14 -O3 -Wno-inconsistent-missing-override -faligned-new -mavx2 -msse2 -msse4.2

          Batching:              yes
    Flow Subsystem:              yes
 Context Subsystem:              no
       HW Flow API:              no

    DPDK   support:              yes
    DPDK    Target:
    DPDK   Version:              22.11
    DPDK      Path:              /etinfo/users2/tbarbette/workspace/dpdk-stable-22.11.1-nains
    DPDK     build:              meson
    DPDK  Location:              /etinfo/users2/tbarbette/workspace/dpdk-stable-22.11.1-nains/install/lib/x86_64-linux-gnu

    RSS++  support:              yes

    Netmap support:              no
    PCAP   support:              yes

    PAPI   support:              no
    RE2    support:              yes
    BPF    support:              yes
    HTTPD  support:              yes
    libpci support:              yes
    LLVM   support:              yes

Now type make to build FastClick...
```

Here's an example with the configure line to **enable LLVM**:
```
./configure --disable-linuxmodule --enable-userlevel --enable-user-multithread --enable-etherswitch --disable-dynamic-linking --enable-local --enable-dpdk=$XCHG_SDK --enable-research --disable-task-stats --enable-flow --enable-cpu-load --prefix $(pwd)/build/ --enable-intel-cpu --enable-dpdk-pool --enable-rand-align RTE_TARGET=x86_64-native-linux-clanglto CXX="clang++ -flto -fno-access-control" CC="clang -flto" CXXFLAGS="-std=gnu++14 -O3" LDFLAGS="-flto -fuse-ld=lld -Wl,-plugin-opt=save-temps" RANLIB="/bin/true" LD="ld.lld" READELF="llvm-readelf" AR="llvm-ar" --disable-bound-port-transfer --enable-dpdk --enable-dpdk-pool --disable-dpdk-packet --disable-dpdk-softqueue
[...]
Configuration Summary:
    C     Compiler:              clang -flto
    C++   Compiler:              clang++ -flto -fno-access-control
    CFLAGS:                      -march=native -g -O2 -W -Wall -mavx2 -msse2 -msse4.2
    CXXFLAGS:                    -march=native -std=gnu++14 -O3 -Wno-inconsistent-missing-override -faligned-new -mavx2 -msse2 -msse4.2

          Batching:              yes
    Flow Subsystem:              yes
 Context Subsystem:              no
       HW Flow API:              no

    DPDK   support:              yes
    DPDK    Target:
    DPDK   Version:              22.11
    DPDK      Path:              /etinfo/users2/tbarbette/workspace/dpdk-stable-22.11.1-nains
    DPDK     build:              meson
    DPDK  Location:              /etinfo/users2/tbarbette/workspace/dpdk-stable-22.11.1-nains/install/lib/x86_64-linux-gnu

    RSS++  support:              yes

    Netmap support:              no
    PCAP   support:              yes

    PAPI   support:              no
    RE2    support:              yes
    BPF    support:              yes
    HTTPD  support:              yes
    libpci support:              yes
    LLVM   support:              yes

Now type make to build FastClick...
```

To enable X-Change too, see the PacketMill's [repo][packetmill-repo].

## Automatic PacketMill

The "packetmill" binary will automatically execute all the instructions below, creating a new binary and then executing it. It is therefore drop-in replacement for the "click" command.

```bash
sudo bin/packetmill --dpdk -- CONFIG
```

## Manual pipeline

The following explains how to execute the pipeline manually.

### Devirtualize Pass

This optimization pass removes virtual function calls from elements' source code based on the input configuration file. More specifically, it duplicates the source code, override some methods, and defines the type of called pointers & the called functions. The following code snippet shows the source code of `FromDPDKDevice::output_push` defined by `click-devirtualize` after applying the pass.

```cpp
inline void
FromDPDKDevice_a_afd0::output_push(int i, Packet *p) const
{
  if (i == 0) { ((Classifier_a_aFNT_a3_sc0 *)output(i).element())->Classifier_a_aFNT_a3_sc0::push(0, p); return; }
  output(i).push(p);
}
```

The new source code replaces the actual call as opposed to the normal FastClick code that uses `output(port).push_batch(batch);`. This pass has been originally introduced by `click-devirtualize`. We have adopted it and modified it to work with FastClick. Fore more information, please check click-devirtualize [paper][devirtualize-paper].

To use this pass, run the following commands:

**Note that you need to compile & install FastClick before applying any pass.**

```bash
sudo bin/click-devirtualize CONFIG  > package.uo
ar x package.uo config
cd userlevel
sudo ../bin/click-mkmindriver -V -C .. -p embed --ship -u ../package.uo
make embedclick MINDRIVER=embed STATIC=0
```

After building `embedclick`, you can run the new click binary with the new configuration file. For instance, you can run the following command:

```bash
sudo userlevel/embedclick --dpdk -l 0-35 -n 6 -w 0000:17:00.0 -v -- config
```

To see the changes to the source code, you can check `clickdv*.cc` and `clickdv*.hh` files in `userlevel/`.

### Replace Pass

This pass replaces the variables with their available value in the configuration file. For instance, the following code snippet shows a part `FromDPDKDevice::run_task` function. Replace pass substitutes `_burst` variable by its value, `32`, which is specified in the input configuration file.

```cpp
// FastClick
unsigned n = rte_eth_rx_burst(_dev->port_id, iqueue, pkts, _burst);

// click-devirtualize + replace pass
unsigned n = rte_eth_rx_burst(_dev->port_id, iqueue, pkts, 32);
```

To use this pass, you have to add `--replace` when using `click-devirtualize`.


### Static Pass

This pass (i) allocates/defines the elements' objects in the source code and (ii) replaces the devirtualized calls with the actual functions. For instance, it defins an object for the `Classifier_a_aFNT_a3_sc0` in the source code and directly replaces the pointer in the `FromDPDKDevice::output_push` function with the actual object. We reintialize the objects at runtime to ensure that variables are intialized properly.


```cpp
inline void
FromDPDKDevice_a_afd0::output_push(int i, Packet *p) const
{
  if (i == 0) { obj_Classifier_a_aFNT_a3_sc0.push(0, p); return; }
  output(i).push(p);
}
```

To use this pass, you have to add `--static` when using `click-devirtualize`. You should also set `STATIC=1` when compiling `embedclick`. Additionaly, you should define the following variables:

```bash
export CLICK_ELEM_RAND_SEED=0
export CLICK_ELEM_RAND_MAX=0
```

These variables can be used to add a randomized offset to the application's layout when defining the objects. We used this feature to ensure that our improvements are not due to the layout bias. Note that they do not have any effect unless you compile FastClick with `--x$enable_rand_align`.

### Inline Pass

This pass add `inline` keyword to some of the functions. To use it, you have to add `--inline` when using `click-devirtualize`.

### Alignas Pass

This pass adds `alignas(x)` keyword to the devirtualized element classes, thereby changing the alignment of objects. We used this feature to ensure that our improvements are not due to the layout bias. To use this pass, you have to add `--alignas x` when using `click-devirtualize`.

### Other Passes

We also worked on a few passes to unroll (or use computed jumps/switch for) the fast path loop. You can check the `click-devirtualize` source code for more information, see [here][devirtualize-code], search for `unroll`, `switch`, and `jmps`.

## X-Change

This modified version of FastClick also support `X-Change` API used by PacketMill. For more information, please refer to PacketMill's [repo][packetmill-repo] and X-Change [repo][x-change-repo].


[packetmill-paper]: https://people.kth.se/~farshin/documents/packetmill-asplos21.pdf
[packetmill-repo]: https://github.com/aliireza/packetmill
[x-change-repo]: https://github.com/tbarbette/xchange
[devirtualize-code]: https://github.com/tbarbette/fastclick/blob/packetmill/tools/click-devirtualize/click-devirtualize.cc
[devirtualize-paper]: https://pdos.csail.mit.edu/~rtm/papers/click-asplos02.pdf
