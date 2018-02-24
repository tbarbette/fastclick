Metron
=========
[Metron][metron-paper] is a high performance and ultra efficient NFV service chaining platform to appear in [USENIX NSDI 2018][metron-nsdi-page].


About
----
Metron's control plane is based on the [ONOS SDN controller][onos], which we extended with [southbound drivers][metron-driver] that allow Metron to monitor and configure commodity servers.
Metron's data plane extends [FastClick][fastclick], which in turn uses [DPDK][dpdk] as a high performance network I/O subsystem.
The Metron data plane uses two features available in modern network interface cards (NICs) to achieve accurate dispatching of input traffic to the desired CPU core(s), thus eliminating inter-core communication.
Specifically, the Metron data plane uses either:
  1. the Virtual Machine Device queues (VMDq) of DPDK to implement hardware dispatching based on the values of input packets' destination MAC address or
  2. DPDK's Flow Director library to classify and dispatch input traffic.
The VMDq mode requires a device prior to the server to tag incoming packets with the correct destination MAC address value, which will be used by the Metron data plane to perform CPU core dispatching.
This task is automatically performed by the Metron controller, using e.g., an OpenFlow switch connected between the source and the NFV server.
The Flow Director mode allows the Metron data plane agent to perform tagging in a more autonomous way, without necessarily involving the controller.
This repository provides the source code of Metron's data plane.


Configure
----
First, setup your DPDK environment (version 17.05 or later) and then configure Metron with:
```bash
./configure RTE_SDK=path-to-dpdk RTE_TARGET=dpdk-target --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose CFLAGS="-std=gnu11 -O3" CXXFLAGS="-std=gnu++14 -O3" --disable-dynamic-linking --enable-poll --enable-bound-port-transfer --enable-dpdk --enable-batch --with-netmap=no --enable-zerocopy --enable-dpdk-pool --disable-dpdk-packet --enable-local --enable-nanotimestamp --enable-all-elements --enable-analysis --enable-json
```


Build
----
make -j <coresNb>


Deploy
----
To deploy Metron agent in VMDq mode, do:
```bash
sudo bin/click --dpdk -c 0xffff -n 4 -w 01:00.0 -w 01:00.1 -v -- conf/metron/metron-master-vmdq.conf
```

To deploy Metron agent in Flow Director mode, do:
```bash
sudo bin/click --dpdk -c 0xffff -n 4 -w 01:00.0 -w 01:00.1 -v -- conf/metron/metron-master-flow-director.conf rulesFile=conf/metron/test_nic_rules
```


Citing Metron
----
If you use Metron in your work, please cite our [paper][metron-paper]:
```
@inproceedings{katsikas-metron.nsdi18,
	author       = {Katsikas, Georgios P. and Barbette, Tom and Kosti\'{c}, Dejan and Steinert, Rebecca and Maguire Jr., Gerald Q.},
	title        = {{Metron: NFV Service Chains at the True Speed of the Underlying Hardware}},
	booktitle    = {To appear in the proceedings of the 15th USENIX Conference on Networked Systems Design and Implementation},
	series       = {NSDI'18},
	year         = {2018},
	url          = {https://people.kth.se/~dejanko/documents/publications/metron-nsdi18.pdf},
	address      = {Renton, WA},
	publisher    = {{USENIX} Association}
}
```


Getting help
----
Use the github [issue tracker][fastclick-issue-tracker] or contact tom.barbette at ulg.ac.be and/or
georgios.katsikas at ri.se if you encounter any problems with Metron.

Please do not report Metron-related or FastClick-related problems on the vanilla Click mailing list.
If you are sure that your problem is Click related, post it on vanilla Click's [issue tracker][click-issue-tracker].

The FastClick reamde is available [here][fastclick-readme].
The  original Click reamde is available [here][click-readme].

[metron-paper]: https://people.kth.se/~dejanko/documents/publications/metron-nsdi18.pdf
[metron-nsdi-page]: https://www.usenix.org/conference/nsdi18/presentation/katsikas
[onos]: https://onosproject.org/
[metron-driver]: https://github.com/opennetworkinglab/onos/tree/master/drivers/server
[fastclick]: https://github.com/tbarbette/fastclick
[dpdk]: https://dpdk.org/
[fastclick-issue-tracker]: https://github.com/tbarbette/fastclick/issues
[click-issue-tracker]: https://github.com/kohler/click/issues
[fastclick-readme]: README.fastclick.md
[click-readme]: README.original.md
