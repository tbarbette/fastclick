// Left side of the connection
ipINLeft :: IPIn();
tcpINLeft :: TCPIn(0, tcpOUTLeft, tcpINRight);
httpINLeft :: HTTPIn();
ipOUTLeft :: IPOut();
tcpOUTLeft :: TCPOut();
httpOUTLeft :: HTTPOut();
reorderLeft :: TCPReorder(0);
retransmitterLeft :: TCPRetransmitter();

// Right side of the connection
ipINRight :: IPIn();
tcpINRight :: TCPIn(1, tcpOUTRight, tcpINLeft);
httpINRight :: HTTPIn();
ipOUTRight :: IPOut();
tcpOUTRight :: TCPOut();
httpOUTRight :: HTTPOut();
reorderRight :: TCPReorder(1);
retransmitterRight :: TCPRetransmitter();

// Left path
FromNetmapDevice(netmap:eth0, PROMISC true) -> Strip(14) -> chIN :: CheckIPHeader()[0] -> ipINLeft -> reorderLeft[0] -> TCPMarkMSS(0, 24) -> tcpINLeft -> httpINLeft -> httpOUTLeft -> tcpOUTLeft -> [0]retransmitterLeft -> ipOUTLeft -> outLeft :: Unstrip(14) -> ToNetmapDevice(netmap:eth1);

// Right path
FromNetmapDevice(netmap:eth1, PROMISC true) -> chOUT :: CheckIPHeader(14)[0] -> Strip(14) -> ipINRight -> reorderRight[0] -> TCPMarkMSS(1, 24) -> tcpINRight -> httpINRight -> InsultRemover() -> httpOUTRight -> tcpOUTRight[0] -> [0]retransmitterRight -> ipOUTRight -> TCPFragmenter(MTU 1480) -> EtherEncap(0x800, 08:00:27:27:b5:9a, 08:00:27:db:83:16) -> outRight:: ToNetmapDevice(netmap:eth0);

// Retransmissions detected by TCPReorder go TCPRetransmitter
reorderLeft[1] -> [1]retransmitterLeft;
reorderRight[1] -> [1]retransmitterRight;

// Left path for generated packets that go back to the source
etherLeft :: EtherEncap(0x800, 08:00:27:27:b5:9a, 08:00:27:db:83:16);
tcpOUTLeft[1] -> etherLeft -> Print(SecondaryOutLeft) -> ToNetmapDevice(netmap:eth0);

// Right path for generated packets that go back to the source
etherRight :: EtherEncap(0x800, 08:00:27:db:83:16, 08:00:27:27:b5:9a);
tcpOUTRight[1] -> etherRight -> Print(SecondaryOutRight) -> ToNetmapDevice(netmap:eth1);

// Non-ip packets bypass the middlebox
chIN[1] -> outLeft;
chOUT[1] -> outRight;
