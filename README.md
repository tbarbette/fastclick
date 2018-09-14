FastClick
=========
This is an extended version of the Click Modular Router featuring an
improved Netmap support and a new DPDK support. It was the result of
our ANCS paper available at http://hdl.handle.net/2268/181954, but received
multiple contributions and improvements since then.

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

Examples
--------
See conf/fastclick/README.md
The wiki provides more information about the [I/O frameworks you should use for high speed](https://github.com/tbarbette/fastclick/wiki/High-speed-I-O), such as DPDK and Netmap, and how to configure them.

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
