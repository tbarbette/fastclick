FastClick
=========
This is an extended version of the Click Modular Router featuring an
improved Netmap support and a new DPDK support. It is the result of
our ANCS paper available at http://hdl.handle.net/2268/181954 .

The [Wiki](https://github.com/tbarbette/fastclick/wiki) provides documentation about the elements and how to use some FastClick features
such as batching.

Contribution
------------
FastClick also aims at keeping a more up-to-date fork and welcomes
contributions from anyone.

Regular contributors will be given direct access to the repository.
The general rule of thumb to accept a pull request is to involve
two different entities. I.e. someone for company A make a PR and
someone from another company/research unit merges it.

Netmap
------
Be sure to install Netmap on your system then configure with :
```bash
./configure --with-netmap --enable-netmap-pool --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose --enable-select=poll CFLAGS="-O3" CXXFLAGS="-std=gnu++11 -O3"  --disable-dynamic-linking --enable-poll --enable-bound-port-transfer --enable-local --enable-zerocopy --enable-batch
```
to get the better performances.

An example configuration is :
```
FromNetmapDevice(netmap:eth0) -> CheckIPHeader() -> ToNetmapDevice(netmap:eth1)
```

To run click, do :
```bash
sudo bin/click -j 4 -a /path/to/config/file.click
```
Where 4 is the number of threads to use. The FromNetmapDevice will share the assigned cores themselves, do not pin the thread.

We noted that Netmap performs better without MQ, or at least with a minimal amount of queues :
ethtool -L eth% combined 1
will set the number of Netmap queues to 1. No need to pin the IRQ of the queues as our FastClick implementation will
take care of it. Just kill irqbalance.

Also, be sure to read the sections of our paper about full push to make the faster configuration.

The `--enable-netmap-pool` option allows to use Netmap buffers instead of Click malloc'ed buffers. This enhance performance as there is only one kind of buffer floating into Click. However with this option you need to place at least one From/ToNetmapDevice in your configuration and allocate enough Netmap buffers using NetmapInfo.

DPDK
----
Partial DPDK support is now reverted into vanilla Click (without support for
batching, auto-thread assignment, thread vector, ...).

Setup your DPDK environment (version 1.6 to 17.05 are supported), then configure with :
```bash
./configure --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose CFLAGS="-g -O3" CXXFLAGS="-g -std=gnu++11 -O3" --disable-dynamic-linking --enable-poll --enable-bound-port-transfer --enable-dpdk --enable-batch --with-netmap=no --enable-zerocopy --enable-dpdk-pool --disable-dpdk-packet
```
to get the better performances.

An example configuration is :
```
FromDPDKDevice(0) -> CheckIPHeader(OFFSET 14) -> ToDPDKDevice(1)
```

To run click with DPDK, you can add the usual EAL parameters :
```bash
sudo bin/click --dpdk -c 0xf -n 4 -- /path/to/config/file.click
```
where 4 is the number of memory channel and 0xf the core mask.

DPDK only supports full push mode.

As for Netmap `--enable-dpdk-pool` option allows to use only DPDK buffers instead of Click malloc'ed buffers.
The `--enable-dpdk-packet` option allows to use DPDK packet handling mechanism instead of of Click's Packet object. All Packet function will be changed by wrappers around DPDK's rte\_pktmbuf functions. However this feature while reducing memory footprint do not enhance the performances as Packets objects are recyced in LIFO and stays in cache while every new access to metadata inside the rte\_mbuf produce a cache miss.

Examples
--------
See conf/fastclick/README.md

Continuous integration and `make check`
---------------------------------------
To ensure people not familiar with batching get warned about bad configuration including non-batch compatible element, some messages are printed to inform a potential slower configuration. However testies (used by `make check`) does not cope well with those message to stdout. To disable them and allow make check to run, you must pass `--disable-verbose-batch` to configure.

This repository uses Travis CI for CI tests which run make check under various configure options combinations. We also have a Gitlab CI for internal tests.

Differences with the ANCS paper
-------------------------------
For simplicity, we reference all input element as "FromDevice" and output
element as "ToDevice". However in practice our I/O elements are 
FromNetmapDevice/ToNetmapDevice and FromDPDKDevice/ToDPDKDevice. They both
inherit from QueueDevice, which is a generic abstract element to implement a
device which supports multiple queues (or in a more generic way I/O through
multiple different threads).

Thread vector and bit vector designate the same thing.

The --enable-dpdk-packet flag allows to use the metadata of the DPDK packets
and use the click Packet class only as a wrapper, as such the Click buffer
and the Click pool is completly unused. However we did not spoke of that feature
in the paper as this doesn't improve performance. DPDK metadata is written
in the beginning of the packet buffer. And writing the huge Click annotation
space (~164 bytes) leads to more cache miss than with the Click pool where a
few Click Packet descriptors are re-used to "link" to differents DPDK buffers
using the pool recycling mechanism. Even when reducing the annotation to a
minimal size (dpdk metadata + next + prev + transport header + ...) this still
force us to fetch a new cacheline.


Getting help
------------
Use the github issue tracker (https://github.com/tbarbette/fastclick/issues) or
contact barbette at kth.se if you encounter any problem.

Please do not ask FastClick-related problems on the vanilla Click mailing list.
If you are sure that your problem is Click related, post it on vanilla Click's
issue tracker (https://github.com/kohler/click/issues).

The original Click readme is available in the README.original file.
