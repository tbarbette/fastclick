Examples of FastClick configurations
====================================

This folder features some examples of FastClick configuration.

Examples are named according to a function, a layer and sometimes a suffix according to the following list. As an example, switch-l2.click features a simple learning switch.

Where to begin with
-------------------
An interesting example is to compare router-vanilla.click and router.click, which implement the basic Click router using respectively vanilla Click and FastClick to emphase the easier configuration in multiqueue + multithread context, perhaps the better improvement of FastClick after performances, but more difficult to show in the paper. Note that if you run router-vanilla on FastClick, the improvement made to Click core system will already improve the performances as opposed to running it in vanilla Click directly.

To learn FastClick configuration, start with pktgen-l1.click which implement a packet generator with static ethernet configuration, and then tester-l3.click which implement a L3 pktgen but also a receiver for those packet to test the amount of packet lossed and the throughput of some device in between.

Name
----
- switch-\*      : Basic simple example forwarding packet
- pktgen-\*      : Packet generator
- receiver-\*    : Receiver for above packet generator
- tester-\*      : Statistic system combining a sender and a receiver to compute loss rate
- router-\*      : Router (could be named switch-l3, but it is more standard)

Layer
-----
- -l1-   : Link layer, pure cable test
- -l2-   : Work if plugged into a L2 switch / mac address are setted correctly (if parametered)
- -l3-   : Include ARP element to answer to ARP request, should work with a L3 router if the given IP, GW, etc are correct

Suffix
------
- -dual    : Bidirectionnal test
- -loop    : Repeat a test in loop changing the given packet size. Usefull to make a graph as in the FastClick paper (packet size vs throughput)
- -vanilla : Use only vanilla Click elements.
