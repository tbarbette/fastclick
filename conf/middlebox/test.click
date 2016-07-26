// Left side of the connection
define($leftMac 08:00:27:db:83:16)
ipINLeft :: IPIn();
tcpINLeft :: TCPIn(0, tcpOUTLeft, tcpINRight); // 0 is the id of the flow direction
httpINLeft :: HTTPIn();
ipOUTLeft :: IPOut();
tcpOUTLeft :: TCPOut();
httpOUTLeft :: HTTPOut();
reorderLeft :: TCPReorder(0); // 0 is the id of the flow direction
retransmitterLeft :: TCPRetransmitter();

// Right side of the connection
define($rightMac 08:00:27:27:b5:9a)
ipINRight :: IPIn();
tcpINRight :: TCPIn(1, tcpOUTRight, tcpINLeft); // 1 is the id of the flow direction
httpINRight :: HTTPIn();
ipOUTRight :: IPOut();
tcpOUTRight :: TCPOut();
httpOUTRight :: HTTPOut();
reorderRight :: TCPReorder(1); // 1 is the id of the flow direction
retransmitterRight :: TCPRetransmitter();

// Left path
inLeft:: FromNetmapDevice(netmap:eth0, PROMISC true) -> chLeft :: CheckIPHeader(14)[0] -> Strip(14)
    -> ipINLeft -> reorderLeft[0] -> tcpINLeft -> httpINLeft -> httpOUTLeft -> tcpOUTLeft
    -> [0]retransmitterLeft -> ipOUTLeft
    -> TCPMarkMSS(0, 24, OFFSET 40) -> TCPFragmenter(MTU 1500, MTU_ANNO 24)
    -> EtherEncap(0x800, $leftMac, $rightMac) -> outLeft :: ToNetmapDevice(netmap:eth1);

// Right path
inRight:: FromNetmapDevice(netmap:eth1, PROMISC true) -> chRight :: CheckIPHeader(14)[0] -> Strip(14)
    -> ipINRight -> reorderRight[0] -> tcpINRight -> httpINRight -> InsultRemover()
    -> httpOUTRight -> tcpOUTRight[0] -> [0]retransmitterRight -> ipOUTRight
    -> TCPMarkMSS(1, 24, OFFSET 40) -> TCPFragmenter(MTU 1500, MTU_ANNO 24)
    -> EtherEncap(0x800, $rightMac, $leftMac) -> outRight:: ToNetmapDevice(netmap:eth0);

// Retransmissions detected by TCPReorder go TCPRetransmitter
reorderLeft[1] -> [1]retransmitterLeft;
reorderRight[1] -> [1]retransmitterRight;

// Left path for generated packets that go back to the source
etherLeft :: EtherEncap(0x800, $rightMac, $leftMac);
tcpOUTLeft[1] -> etherLeft -> ToNetmapDevice(netmap:eth0);

// Right path for generated packets that go back to the source
etherRight :: EtherEncap(0x800, $leftMac, $rightMac);
tcpOUTRight[1] -> etherRight -> ToNetmapDevice(netmap:eth1);

// Non-ip packets bypass the middlebox
chLeft[1] -> outLeft;
chRight[1] -> outRight;
