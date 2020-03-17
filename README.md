MiddleClick
===========


## Compilation

Compile MiddleClick with:
```
./configure --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose CFLAGS="-g -O3" CXXFLAGS="-g -std=gnu++11 -O3" --disable-dynamic-linking --enable-poll --enable-bound-port-transfer --enable-dpdk --enable-batch --with-netmap=no --enable-zerocopy --disable-dpdk-pool --disable-dpdk-packet --enable-user-timestamp --enable-flow --enable-ctx
```
Only the last 2 options are specific to MiddleClick, to enable the flow subsystem and the context subsystem. As MiddleClick use some timers, the --enable-user-timestamp option is a good idea too.

## Sample configurations

You may find sample configurations in conf/middleclick/. Please check the README.md file for description of the sample configurations.

## Contact

To not hesitate to contact barbette@kth.se for more informations.
