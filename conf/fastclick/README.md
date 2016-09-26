Example name convention
=======================

Name
----
switch-\*        : Basic simple example forwarding packet
pktgen-\*      : Packet generator
receiver-\*    : Receiver for above packet generator
tester-\*      : Statistic system combining a sender and a receiver to compute loss rate

Layer
-----
-l1-   : Link layer, pure cable test
-l2-   : Work if plugged into a L2 switch / mac address are setted correctly (if parametered)
-l3-   : Include ARP element to answer to ARP request, should work with a L3 router if the given IP, GW, etc are correct

Suffix
------
-dual  : Bidirectionnal test
