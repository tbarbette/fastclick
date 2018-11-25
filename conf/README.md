# Examples of configurations

This folder and its subfolders features some examples of (Fast)Click
 configuration.

Folders are organised by demonstrated feature. But some example of course
 demonstrate multipe interesting features at once, so do not hesitate to
 grep for element names if looking for examples.

## Sub-folders

 * app         Application-layer
 * deprecated  Deprecated examples (because of deprecated elements)
 * dpdk        DPDK-specific
 * grid        Grid
 * ip6         IPv6
 * kernel      Kernel-specific feature
 * lib         Library of elementclass. Include them with require(library conf/lib/*.click)
 * nat         NAT and LB
 * pktgen      Packet generation
 * proxy       Proxy
 * queueing    Advanced queuing, early dropping, ...
 * ron         RON
 * router      Router
 * sched       Task scheduling
 * script      Scripting
 * simple      Simple examples
 * snoop       TCPSnoop element
 * test        Testing
 * tools       Related to Click tools and scripts to build configs
 * vpn         VPN
 * wifi        WiFi


## Cross-disciplinary topics

### FastClick vs Click
An interesting example is to compare router-vanilla.click and router.click, which implement the basic Click router using respectively vanilla Click and FastClick to emphase the easier configuration in multiqueue + multithread context, perhaps the better improvement of FastClick after performances, but more difficult to show in the paper. Note that if you run router-vanilla on FastClick, the improvement made to Click core system will already improve the performances as opposed to running it in vanilla Click directly.

