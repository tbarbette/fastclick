FastClick
=========
This is an extended version of the Click Modular Router featuring an
improved Netmap support and a new DPDK support. It is the result of
our ANCS paper available at http://hdl.handle.net/2268/181954
You'll find more details on http://fastclick.run.montefiore.ulg.ac.be

Partial DPDK support is now reverted into vanilla Click (without support for batching, auto-thread assignment, thread vector, ...).

Netmap
------
Be sure to install Netmap on your system then configure with :
./configure --with-netmap --enable-netmap-pool --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose --enable-select=poll CFLAGS="-O3" CXXFLAGS="-std=gnu++11 -O3"  --disable-dynamic-linking --enable-poll --enable-bound-port-transfer --enable-local --enable-zerocopy --enable-batch
to get the better performances.

An example configuration is :
FromNetmapDevice(netmap:eth0) -> CheckIPHeader() -> ToNetmapDevice(netmap:eth1)

To run click, do :
sudo bin/click -j 4 -a /path/to/config/file.click
Where 4 is the number of threads to use. The FromNetmapDevice will share the assigned cores themselves, do not pin the thread.

We noted that Netmap performs better without MQ, or at least with a minimal amount of queues :
ethtool -L eth% combined 1
will set the number of Netmap queues to 1. No need to pin the IRQ of the queues as our FastClick implementation will
take care of it. Just kill irqbalance.

Also, be sure to read the sections of our paper about full push to make the faster configuration.

The --enable-netmap-pool option allows to use Netmap buffers instead of Click malloc'ed buffers. This enhance performance as there is only one kind of buffer floating into Click. However with this option you need to place at least one From/ToNetmapDevice in your configuration and allocate enough Netmap buffers using NetmapInfo.

DPDK
----
Setup your DPDK 1.6 to 2.2 environment, then configure with :
./configure --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose CFLAGS="-g -O3" CXXFLAGS="-g -std=gnu++11 -O3" --disable-dynamic-linking --enable-poll --enable-bound-port-transfer --enable-dpdk --enable-batch --with-netmap=no --enable-zerocopy --enable-dpdk-pool --disable-dpdk-packet
to get the better performances.

An example configuration is :
FromDPDKDevice(0) -> CheckIPHeader(OFFSET 14) -> ToDPDKDevice(1)

To run click with DPDK, you can add the usual EAL parameters :
sudo bin/click -c 0xf -n 4 -- /path/to/config/file.click
where 4 is the number of memory channel and 0xf the core mask.

DPDK only supports full push mode.

As for Netmap --enable-dpdk-pool option allows to use only DPDK buffers instead of Click malloc'ed buffers.
The --enable-dpdk-packet option allows to use DPDK packet handling mechanism instead of of Click's Packet object. All Packet function will be changed by wrappers around DPDK's rte\_pktmbuf functions. However this feature while reducing memory footprint do not enhance the performances as Packets objects are recyced in LIFO and stays in cache while every new access to metadata inside the rte\_mbuf produce a cache miss.

How to make an element batch-compatible
---------------------------------------
FastClick is backward compatible with all vanilla element, and it should work 
out of the box with your own library. However Click may un-batch and re-batch
packets along the path. This is not an issue for most slow path elements such 
as ICMP erros elements, where the development cost is not worth it. However, 
you probably want to have only batch-compatible elements in your fast path as 
this will be really faster.

Batch-compatible element should extend the BatchElement instead of the Element 
class. They also have to implement push\_packet instead of push, and 
a version of push receiving PacketBatch\* instead of Packet\* called 
push\_batch. 

The reson why batch element must provide a good old push\_packet version is that
it may be not worth it to rebuild a batch before your element, and then 
unbatch-it because your element is betweem to vanilla elements. In this case the
push\_packet version of your element will be used.

To let click compile with --disable-batch, always enclose push\_batch prototype
and implementation around #if HAVE\_BATCH .. #endif

If your element must use batching, if only push\_batch is implemented or 
your element always produces batches no matter the input, you
will want to set batch\_mode=BATCH\_MODE\_YES in the constructor, to let know 
the backward-compatibility manager that subsequent elements will receive 
batches, and previous element must send batch or let the backward compatibility 
manager rebuild a batch before passing it to your element. The default is 
BATCH\_MODE\_IFPOSSIBLE, telling that it should run in batch mode if it can, and 
vanilla element are fixed to BATCH\_MODE\_NO.

FYI, the reason why push has been renamed to push\_packet is that when a 
non-batch combatible element want to call push on your element, it wants to call 
PacketBatch::push and not your version because the former one will actually 
rebuild a batch if needed and call your element's putch\_batch instead.


Getting help
------------
Use the github issue tracker or contact tom.barbette at ulg.ac.be if you encounter any problem.

Please do not ask FastClick-related problems on the vanilla Click mailing list.

The original Click readme is available in the README.original file.
