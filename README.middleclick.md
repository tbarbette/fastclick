MiddleClick
===========

## Paper
MiddleClick brought in (Fast)Click the ability to understand flows and sessions on top of packets. MiddleClick allows each elements to require some "session space", such as an Object for every different 4-tuple. A unique element will then collect the space needed accross parallel path, compute a minimal session space and pre-allocate enough bytes for every elements along the path. It will also use a new context definition to minimize traffic class classification along paths, and enable implicit classification, removing the need to add Classifier everywhere. MiddleClick also brought in the ability to modify TCP stream on the flight using this new context system.
[Check the paper](https://www.diva-portal.org/smash/record.jsf?pid=diva2%3A1582880&dswid=810) for more details.

## Compilation

Compile MiddleClick with:
```
./configure --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose CFLAGS="-g -O3" CXXFLAGS="-g -std=gnu++11 -O3" --disable-dynamic-linking --enable-poll --enable-bound-port-transfer --enable-dpdk --enable-batch --with-netmap=no --enable-zerocopy --disable-dpdk-pool --disable-dpdk-packet --enable-user-timestamp --enable-flow --enable-ctx
```
Only the last 2 options are specific to MiddleClick, to enable the flow subsystem and the context subsystem. As MiddleClick use some timers, the --enable-user-timestamp option is a good idea too.

## Sample configurations

You may find sample configurations in conf/middleclick/. Please check the README.md file for description of the sample configurations.

## Contact

Use the FastClick GitHub issues preferably but do not hesitate to contact tom.barbette@uclouvain.be for more information if needed.
