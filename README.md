FastClick
=========
This is an extended version of the Click Modular Router featuring an
improved Netmap support and a new DPDK support. It is the result of
our ANCS paper available at http://hdl.handle.net/2268/181954
You'll find more details on http://fastclick.run.montefiore.ulg.ac.be

The last version is available on github, while the gitlab repo available
in the last link have multiple branches to look at each features
independently as we do in our paper.

Netmap
------
Be sure to install Netmap on your system then configure with :
./configure --with-netmap --enable-netmap-pool --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose --enable-select=poll CFLAGS="-O3" CXXFLAGS="-std=gnu++11 -O3"  --disable-dynamic-linking --enable-poll --enable-bound-port-transfer --enable-local --enable-zerocopy --enable-batch
to get the better performances.

An example configuration is :
FromNetmapDevice(0) -> CheckIPHeader() -> FromNetmapDevice(0)

To run click, do :
sudo bin/click -j 4 -a /path/to/config/file.click
Where 4 is the number of threads to use. The FromNetmapDevice will share the assigned cores themselves, do not pin the thread.

We noted that Netmap performs better without MQ, or at least with a minimal amount of queues :
ethtool -L eth% combined 1
will set the number of Netmap queues to 1. No need to pin the IRQ of the queues as our FastClick implementation will
take care of it. Just kill irqbalance.

Also, be sure to read the sections of our paper about full push to make the faster configuration.

DPDK
----
Setup your DPDK 1.7 environment, then configure with :
./configure --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose CFLAGS="-g -O3" CXXFLAGS="-g -std=gnu++11 -O3" --disable-dynamic-linking --enable-poll --enable-bound-port-transfer --enable-dpdk --enable-batch --with-netmap=no --enable-zerocopy --disable-dpdk-pools
to get the better performances.

An example configuration is :
FromDpdkDevice(0) -> CheckIPHeader() -> ToDpdkDevice(0)

To run click with DPDK, you can add the usual EAL parameters :
sudo bin/click -c 0xf -n 4 -- /path/to/config/file.click
where 4 is the number of memory channel and 0xf the core mask.

DPDK only supports full push mode.

Getting help
------------
We tried to rebase our implementation to allow you to enable and disable features through
the multiple branch of http://fastclick.run.montefiore.ulg.ac.be and numerous flags that
you can see with ./configure --help. However this may have introduced some bugs, feel free
to contact tom.barbette at ulg.ac.be if you encounter any problem.

Please do not ask FastClick-related problems on the mailing list.

The original Click readme is available in the README.original file.
