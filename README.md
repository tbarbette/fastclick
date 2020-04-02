# Building FastCick with clang LTO + Applying Click-Devirtualize

This repo contains some tools optimizing the FastClick. You need to install `Clang 10.0 + LLVM 10.0` toolchain before following the instructions. To install LLVM, you can use [llvm-clang.sh](https://bitbucket.org/nslab/llvm-project/src/master/llvm-clang.sh).

* Configure FastClick with:

```bash
./configure RTE_SDK=/home/alireza/llvm-rack13/dpdk-nslab RTE_TARGET=x86_64-native-linux-clanglto --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose CC="clang -flto" CFLAGS="-std=gnu11 -O3" CXX="clang++ -flto" CXXFLAGS="-std=gnu++14 -O3" LDFLAGS="-flto -fuse-ld=lld -Wl,-plugin-opt=save-temps" RANLIB="/bin/true" LD="ld.lld" READELF="llvm-readelf" AR="llvm-ar" --disable-dynamic-linking --enable-poll --enable-dpdk --disable-dpdk-pool --disable-dpdk-packet
```

* Build FastClick

```bash
make
```

* For dependency reasons, FastClick must be installed system-wide:

```bash
sudo make install
```
  
  **If you installed Click system-wide before, you must do `sudo make uninstall` first because make install will not overwite all changed file!**

* Use click-devirtualize to build a package out of a configuration file

```bash
cp conf/llvm/router.click CONFIG
sudo bin/click-devirtualize CONFIG --inline > package.uo
```

* Extract the generated config

```bash
ar x package.uo config
```

  **You should remove `require(package "clickdv_XX")` in `config` before running `embedclick`.**

* Use click-mkmindriver to create an embedded click, with the new --ship option to embed the code

```bash
cd userlevel
../bin/click-mkmindriver -V -C .. -p embed --ship -u ../package.uo
make MINDRIVER=embed
cd ..
```

  **Note the clickdvXX.cc and .hh file will be kept in userlevel, it contains the code from
  click-devirtualized with all specialized elements and the beetlemonkey to generate our
  limited set of elements. You may recompile it with the following command (the command should be revised when using clang). However, this was already done for you by `click-mkmindriver`. Use make `MINDRIVER=embed V=1` to see the few lines to type to compile a new embedclick manually.**

```bash
g++ -fPIC -flto -std=c++11 -g -O3 -I${RTE_SDK}/x86_64-native-linuxapp-gcc/include -include ${RTE_SDK}/x86_64-native-linuxapp-gcc/include/rte_config.h -Wno-pmf-conversions -faligned-new -c -o   clickdv_Q3Ysjsm0iWjr6UUA6pNNyd.u.o clickdv_Q3Ysjsm0iWjr6UUA6pNNyd.u.cc -fno-access-control
```

* Following the previous steps will generate IR bitcode for `click` and `embedclick` in the `userlevel` directory. Check for `*.bc` files (e.g., `click.0.5.precodegen.bc` and `embedclick.0.5.precodegen.bc`).

* Now, you can simply use `embedclick` to run the `config` (configuration file):

```bash
sudo userlevel/embedclick --dpdk -l 0-3 -- config
```

## Optimize Click or Embedclick

You can use the optimization passes in [llvm-project/FastClick-Pass](https://bitbucket.org/nslab/llvm-project/src/master/FastClick-Pass/) to optimize the `click` or `embedclick` IR bitcodes.

* Creating human-reabale format for IR bitcode:

```bash
cd userlevel/
llvm-dis embedclick.0.5.precodegen.bc -o embedclick-orig.ll
```

### Optimizing the IR bitcode

1. The first pass removes module flags fromthe IR bitcode since it clang not let relinking.

```bash
opt -S -load ~/llvm-rack13/llvm-project/FastClick-Pass/build/class-stripmoduleflags-pass/libClassStripModuleFlagsPass.so -strip-module-flags embedclick-orig.ll -o embedclick-tmp.ll
```

2. The second pass optimize the `class Packet` in Click. It requires list of used elements, which can be created via:

```bash
grep case elements_embed.cc | awk -F"new" '{print $2}' | awk '{print $1}' | awk -F";" '{print $1}' > elements_embed_router.list
```

* Applying the pass:

```bash
opt -S -load ~/llvm-rack13/llvm-project/FastClick-Pass/build/class-handpick-pass/libClassHandpickPass.so -handpick-packet-class embedclick-tmp.ll -element-list-filename elements_embed_router.list -o embedclick-opt.ll
```

3. The third pass replaces the driver virtual calls (e.g., `mlx5_rx_burst_vec` in the FromDPDKDevice elements). It can also inline the calls, but it does not work when compiling DPDK with default configuration.

```bash
opt -S -load ~/llvm-rack13/llvm-project/FastClick-Pass/build/class-driverinline-pass/libClassDriverInlinePass.so -inline-driver embedclick-opt.ll -o embedclick.ll
```

* Creating the final binary:

```bash
cd userlevel/
make embedclick-opt
```

 **Note that the Makefile rule requires `embedclick.ll`. Therefore, if you are skipping any of the above passes, you have to rename the final output.**

 This command generates a new binary called `embedclick-opt`, which can be used similar to `embedclick`.

```bash
sudo bin/embedclick-opt --dpdk -l 0-3 -- config
```

 **The same optimization can be done for the `click`. You can create the binary with `make click-opt`.**

 For more info, check [This](https://docs.google.com/document/d/1O7W9HL8LkKsdq_om_K9bjV7jC-M7c-qeB77CsJ3jA_E/edit?usp=sharing).
