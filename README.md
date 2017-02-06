FastClick
=========
This is an extended version of the Click Modular Router featuring an
improved Netmap support and a new DPDK support. It is the result of
our ANCS paper available at http://hdl.handle.net/2268/181954 .

Partial DPDK support is now reverted into vanilla Click (without support for 
batching, auto-thread assignment, thread vector, ...).

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
Setup your DPDK 1.6 to 2.2 environment, then configure with :
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

How to make an element batch-compatible
---------------------------------------
FastClick is backward compatible with all vanilla element, and it should work 
out of the box with your own library. However Click may un-batch and re-batch
packets along the path. This is not an issue for most slow path elements such 
as ICMP erros elements, where the development cost is not worth it. However, 
you probably want to have only batch-compatible elements in your fast path as 
this will be really faster.

Batch-compatible element should extend the BatchElement instead of the Element 
class. They also have to implement 
a version of push receiving a PacketBatch\* argument instead of Packet\* called 
push\_batch. 

The reason why batch element must provide a good old push fonction is that
it may be not worth it to rebuild a batch before your element, and then 
unbatch-it because your element is betweem two vanilla elements. In this case
the push version of your element will be used.

To let click compile with `--disable-batch`, always enclose push\_batch prototype
and implementation around #if HAVE\_BATCH .. #endif

If your element must use batching, if only push\_batch is implemented or 
your element always produces batches no matter the input, you
will want to set batch\_mode=BATCH\_MODE\_YES in the constructor, to let know 
the backward-compatibility manager that subsequent elements will receive 
batches, and previous element must send batch or let the backward compatibility 
manager rebuild a batch before passing it to your element. The default is 
BATCH\_MODE\_IFPOSSIBLE, telling that it should run in batch mode if it can, and 
vanilla element are fixed to BATCH\_MODE\_NO.

If you provide `--enable-auto-batch`, the vanilla Elements will be set in mode 
BATCH\_MODE\_IFPOSSIBLE, with a special push\_batch function which will simply
call push() for each packets. However the push ports of the elements will
rebuild batches instead of letting them go through.

Without auto-batch, the batches will be un-batched before a vanilla Element and
re-batched when hitting the next BatchElement. It is referenced as the "jump"
mode as the batch "jump over" the vanilla Element. This is the behaviour
described in the ANCS paper and still the default mode.

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
contact tom.barbette at ulg.ac.be if you encounter any problem.

Please do not ask FastClick-related problems on the vanilla Click mailing list.
If you are sure that your problem is Click related, post it on vanilla Click's
issue tracker (https://github.com/kohler/click/issues).

The original Click readme is available in the README.original file.
