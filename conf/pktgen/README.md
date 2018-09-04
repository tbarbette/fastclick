Examples of FastClick configurations
====================================

This folder features some packet generation method using FastClick (that is, 
 most examples relies on FastClick elments and cannot be used with vanilla
 Click, it is not just about performance).

Examples are named according to a function, a layer and sometimes a suffix
according to the following list. As an example, pktgen-l2.click features a L2
pktgen (it won't use ARP or such as it is purely L2).

Where to begin with
-------------------
To learn FastClick configuration, start with pktgen-l2.click which implement a
packet generator with static ethernet configuration, and then tester-l3.click
which implement a L3 pktgen but also a receiver for those packet to test the
amount of packet lossed and the throughput of some device in between.

Name
----
- pktgen-\*      : Packet generator
- receiver-\*    : Receiver for above packet generator
- tester-\*      : Statistic system combining a sender and a receiver to
                    compute loss rate

Layer
-----
- -l2-   : Work if plugged into a L2 switch / mac address are setted
            correctly (if parametered)
- -l3-   : Include ARP element to answer to ARP request, should work with a
            L3 router if the given IP, GW, etc are correct

Suffix
------
- -dual    : Bidirectionnal test
- -loop    : Repeat a test in loop changing the given packet size. Usefull to
              make a graph as in the FastClick paper (packet size vs throughput)
