# Building FastCick with clang LTO + Applying Click-Devirtualize
====

 * Compile Click with:
```
 ./configure RTE_SDK=/home/alireza/llvm-rack13/dpdk-nslab RTE_TARGET=x86_64-native-linux-clangflto --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose CC="clang -flto" CXX="clang++ -flto" LDFLAGS="-flto -Wl,-plugin-opt=save-temps" RANLIB="/bin/true" AR="llvm-ar" --disable-dynamic-linking --enable-poll --enable-dpdk --disable-dpdk-pool --disable-dpdk-packet 
```

 * For dependency reasons, FastClick must be installed system-wide:
```
 sudo make install
```
  
  **If you installed Click system-wide before, you must do `sudo make uninstall` first because make install will not overwite all changed file!**

 * Use click-devirtualize to build a package out of a configuration file

```
cp conf/llvm/router.click CONFIG
sudo bin/click-devirtualize CONFIG --inline > package.uo
```

 * Extract the generated config

```
ar x package.uo config
```

  **You should remove the package requirement in `config`.

 * Use click-mkmindriver to create an embedded click, with the new --ship option to embed the code

```
  cd userlevel
  ../bin/click-mkmindriver -V -C .. -p embed --ship -u ../package.uo
  make MINDRIVER=embed
  cd ..
```
  Note the clickdvXX.cc and .hh file will be kept in userlevel, it contains the code from
  click-devirtualized with all specialized elements and the beetlemonkey to generate our
  limited set of elements. You may recompile it with
  `g++ -fPIC -flto -std=c++11 -g -O3 -I${RTE_SDK}/x86_64-native-linuxapp-gcc/include -include ${RTE_SDK}/x86_64-native-linuxapp-gcc/include/rte_config.h -Wno-pmf-conversions -faligned-new -c -o   clickdv_Q3Ysjsm0iWjr6UUA6pNNyd.u.o clickdv_Q3Ysjsm0iWjr6UUA6pNNyd.u.cc -fno-access-control`
  but that was already done for you by click-mkmindriver. Use make MINDRIVER=embed V=1 to see the few lines to type to compile a new embedclick manually.

 
 * This will generate IR bitcode for `click` and `embedclick` in the `userlevel` directory. Check for `*.bc` files.
 
 * Now simply use embedclick with the configuration file "config", eg:

```
 * sudo userlevel/embedclick --dpdk -l 0-3 -- config
```




## Optimize Click or Embedclick



You can use the optimization passes in [llvm=project](https://bitbucket.org/nslab/llvm-project/src/master/FastClick-Pass/) to optimize the `click` or `embedclick` IR bitcodes.


 * Creating human-reabale format for IR bitcode:
 ```
 llvm-dis embedclick.0.5.precodegen.bc -o embedclick-orig.ll
 ```
 
 * Optimizing the IR bitcode:
 
 The first pass removes module flags fromthe IR bitcode since it clang not let relinking. 
 The second pass optimize the `class Packet` in Click.
 
 ```
opt -S -load ~/llvm-rack13/llvm-project/FastClick-Pass/build/class-stripmoduleflags-pass/libClassStripModuleFlagsPass.so -strip-module-flags embedclick-orig.ll -o embedclick-tmp.ll
opt -S -load ~/llvm-rack13/llvm-project/FastClick-Pass/build/class-handpick-pass/libClassHandpickPass.so -handpick-packet-class embedclick-tmp.ll -element-list-filename elements_embed_router.list -o embedclick.ll
 ```
 
 * elements_embed_router.list can be created via:
 ```
 grep case userlevel/elements_embed.cc | awk -F"new" '{print $2}' | awk '{print $1}' | awk -F";" '{print $1}' > elements_embed_router.list
 ```
 
 * Creating the final binary:
 ```
 cd userlevel/
 make embedclick-opt
 ```
 The same optimization can be done for the `click`. You can create the binary with `make click-opt`.
 
 For more info, check [This](https://docs.google.com/document/d/1O7W9HL8LkKsdq_om_K9bjV7jC-M7c-qeB77CsJ3jA_E/edit?usp=sharing).