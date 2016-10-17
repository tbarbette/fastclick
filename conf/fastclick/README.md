Examples of FastClick configurations
====================================

This folder features some examples of FastClick configuration.

Examples are named according to a function, a layer and sometimes a suffix according to the following list. As an example, switch-l2.click features a simple learning switch.

Exceptions are router-click and router-fastclick, that implement the basic Click router using respectively vanilla Click and FastClick to emphase the easier configuration in multiqueue + multithread context, perhaps the better improvement of FastClick after throughput improvement, but more difficult to show in the paper.

Name
----
- switch-\*      : Basic simple example forwarding packet
- pktgen-\*      : Packet generator
- receiver-\*    : Receiver for above packet generator
- tester-\*      : Statistic system combining a sender and a receiver to compute loss rate

Layer
-----
- -l1-   : Link layer, pure cable test
- -l2-   : Work if plugged into a L2 switch / mac address are setted correctly (if parametered)
- -l3-   : Include ARP element to answer to ARP request, should work with a L3 router if the given IP, GW, etc are correct

Suffix
------
- -dual  : Bidirectionnal test
- -loop  : Repeat a test in loop changing the given packet size. Usefull to make a graph as in the FastClick paper (packet size vs throughput)
