[FastClick](https://www.fastclick.dev) ![CI](https://github.com/tbarbette/fastclick/workflows/C/C++%20CI/badge.svg)
=========
FastClick is an extended version of the Click Modular Router featuring an
improved Netmap support and a new DPDK support. It was the result of
our ANCS paper available at http://hdl.handle.net/2268/181954, but received
multiple contributions and improvements since then, such as flow support with
MiddleClick, specialized binaries with PacketMill, precise intra-server
load-balancing with RSS++ and many more individual contributions over the years.
[More details below](#merged_work).

The [Wiki](https://github.com/tbarbette/fastclick/wiki) provides documentation about the elements and how to use some FastClick features
such as batching.

Announcements
-------------
Be sure to watch the repository and check out the [GitHub Discussions](https://github.com/tbarbette/fastclick/discussions) to stay up to date!

Quick start (using DPDK for I/O)
--------------------------------

 * Install DPDK and FastClick optional dependencies at once with `./deps.sh`  (or just for DPDK with `sudo apt install libelf-dev build-essential pkg-config zlib1g-dev libnuma-dev python3-pyelftools`). To install DPDK, either follow http://core.dpdk.org/doc/quick-start/. In short, just download it, and since 20.11 you have to compile with meson : `meson setup build && cd build && ninja && sudo ninja install`
 * Build FastClick, with support for DPDK using the following command:

```
./configure --enable-dpdk --enable-intel-cpu --verbose --enable-select=poll CFLAGS="-O3" CXXFLAGS="-std=c++11 -O3"  --disable-dynamic-linking --enable-poll --enable-bound-port-transfer --enable-local --enable-flow --disable-task-stats --disable-cpu-load
make
```

*You will find more information in the [High-Speed I/O wiki page](https://github.com/tbarbette/fastclick/wiki/High-speed-I-O).*

FastClick "Light"
-----------------
FastClick, like Click comes with a lot of features that you may not use. The following options will improve performance further :
```
./configure --enable-dpdk --enable-intel-cpu --verbose --enable-select=poll CFLAGS="-O3" CXXFLAGS="-std=c++11 -O3"  --disable-dynamic-linking --enable-poll --enable-bound-port-transfer --enable-local --enable-flow --disable-task-stats --disable-cpu-load --enable-dpdk-packet --disable-clone --disable-dpdk-softqueue
make
```
 * Disable task stats suppress statistics tracking for advanced task scheduling with e.g. BalancedThreadSched. With DPDK, it's polling anyway... And as far as scheduling is concerned, [RSS++](#rss) has a better solution.
 * Disable CPU load will remove load tracking. That is accounting for a CPU percentage while using DPDK by counting cycles spent in empty runs vs all runs. Accessible with the "load" handler.
 * Enable DPDK packet will remove the ability to use any kind of packets other than DPDK ones. The "Packet" class will become a wrapper to a DPDK buffer. You won't be able tu use MMAP'ed packets, Netmap packets, etc. But in general people using DPDK handle DPDK packets. When playing a trace one must copy the data to a DPDK buffer for transmission anyway. This implementation has been improved since the first version and performs better than the default Packet metadata copying **when passing CLEAR false to FromDPDKDevice**. This means you must do the liveness analysis of metadata by yourself, as you can't assume annotations like the VLAN, the timestamp, etc are 0 by default. It would be bad practice in any case to rely on this kind of default value. 
 * Disable clone will remove the indirect buffer copy. So no more reference counting, no more \_data\_packet. But any packet copy like when using Tee will need to completely copy the packet content, just think sequential. That's most pipeline anyway. And you'll loose the ability to process packets in parallel from multiple threads (without copy). But who does that?
 * Disable DPDK softqueue will disable buffering in ToDPDKDevice. Everything it gets will be sent to DPDK, and the NIC right away. When using batching, it's actually not a problem because when the CPU is busy, FromDPDKDevice will take multiple packets at once (limited to BURST), and you'll effectively send batches in ToDPDKDevice. When the CPU is not busy, and you have one packet per batch because there's no more meat then ... well it's not busy so who cares?
 
Ultimately, FastClick will still be impacted by its high flexibility and the many options it supports in each elements. This is adressed by [PacketMill](#packetmill) by embedding constant parameters, and other stuffs to produce one efficient binary.

Docker
------
We provide a Dockerfile to build FastClick with DPDK. Public images are available too in [docker hub](https://hub.docker.com/r/tbarbette/fastclick-dpdk).

The docker container must run in priviledged mode, and most often in network host mode.

```
sudo docker run -v /mnt/huge:/dev/hugepages -it --privileged --network host tbarbette/fastclick-dpdk:generic --dpdk -- -e "FromDPDKDevice(0) -> Discard;"
```

Note: the default image is build for the "default" arch, it will be suboptimal in term of performances. Check the [Docker wiki page for more information.](https://github.com/tbarbette/fastclick/wiki/Docker)

Contribution
------------
FastClick also aims at keeping a more up-to-date fork and welcomes
contributions from anyone.

Regular contributors will be given direct access to the repository.
The general rule of thumb to accept a pull request is to involve
two different entities. I.e. someone for company A make a PR and
someone from another company/research unit merges it.

*You will find more information about contributions in the [Community Contributions wiki page](https://github.com/tbarbette/fastclick/wiki/Community-Contributions).*

Examples
--------
See conf/fastclick/README.md
The wiki provides more information about the [I/O frameworks you should use for high speed](https://github.com/tbarbette/fastclick/wiki/High-speed-I-O), such as DPDK and Netmap, and how to configure them.

Differences with the mainline Click (kohler/click)
--------------------------------------------------
In a nutshell:
 - Batching
 - The DPDK version in mainline is very limited (no native multi-queue, you have to duplicate elements etc)
 - Thread vectors, allowing easier thread management
 - The flow subsystem that comes from MiddleClick and allow to use many classification algorithm for new improved NAT, Load Balancers, DPI engine (HyperScan, SSE4 string search), Statistics tracking, etc
 - By defaults FastClick compiles with userlevel multithread. You still have to explicitely *--enable-dpdk* if you want fast I/O with DPDK (you do)

*You will find more information about the differences with Click in the [related wiki page](https://github.com/tbarbette/fastclick/wiki/Differences-between-FastClick-and-Click)*

## Merged work
This section references projects that have been merged in.

### RSS++
[RSS++](http://www.diva-portal.org/smash/get/diva2:1371780/FULLTEXT01.pdf) is a NIC-driven scheduler. It is compatible with DPDK (and of course FastClick) application and Kernel applications. The part relevent for FastClick are fully merged in this branch. It provides a solution to automatically scale the number of cores to be used by FromDPDKDevice. Except for its integration of a simulated Metron, RSS++ has been completely merged in.

### PacketMill
[PacketMill](https://packetmill.io) is a serie of optimization to accelerate high-speed packet processing, by building a specialized binary. It has been mostly merged-in. See [README.packetmill.md] for more details.

### MiddleClick
[MiddleClick](https://www.diva-portal.org/smash/record.jsf?pid=diva2%3A1582880&dswid=810)) brought in (Fast)Click the ability to understand flows and sessions on top of packets. See [README.middleclick.md] for more details.

### Differences with the FastClick ANCS paper
This section states the differences between FastClick as in this repository and the original ANCS paper. For simplicity, we reference all input element as "FromDevice" and output
element as "ToDevice". However in practice our I/O elements are 
FromNetmapDevice/ToNetmapDevice and FromDPDKDevice/ToDPDKDevice. They both
inherit from QueueDevice, which is a generic abstract element to implement a
device which supports multiple queues (or in a more generic way I/O through
multiple different threads).

Thread vector and bit vector designate the same thing.

Citing
------
If you use FastClick, please cite as follow:
```
@inproceedings{barbette2015fast,
  title={Fast userspace packet processing},
  author={Barbette Tom, Cyril Soldani and Mathy Laurent},
  booktitle={2015 ACM/IEEE Symposium on Architectures for Networking and Communications Systems (ANCS)},
  pages={5--16},
  year={2015},
  organization={IEEE},
  doi={10.1109/ANCS.2015.7110116}
}
```

Note that if you use technologies built on top of FastClick, it is relevant to cite the related paper too. I.e. if you use/improve the DeviceBalancer element, you should cite RSS++ instead (or on top of FastClick if FastClick is also a base to your own development), and if using the packetmill command to produce a specialized binary, cite PacketMill.

Getting help
------------
Use the github issue tracker (https://github.com/tbarbette/fastclick/issues) or
contact barbette at kth.se if you encounter any problem.

Please do not ask FastClick-related problems on the vanilla Click mailing list.
If you are sure that your problem is Click related, post it on vanilla Click's
issue tracker (https://github.com/kohler/click/issues).

The original Click readme is available in the README.original file.
