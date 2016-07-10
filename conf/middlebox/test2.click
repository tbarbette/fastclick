ipINLeft :: IPIn();
tcpINLeft :: TCPIn(0, tcpOUTLeft, tcpINRight);
httpINLeft :: HTTPIn();
ipOUTLeft :: IPOut();
tcpOUTLeft :: TCPOut();
httpOUTLeft :: HTTPOut();

ipINRight :: IPIn();
tcpINRight :: TCPIn(1, tcpOUTRight, tcpINLeft);
httpINRight :: HTTPIn();
ipOUTRight :: IPOut();
tcpOUTRight :: TCPOut();
httpOUTRight :: HTTPOut();

FromNetmapDevice(netmap:eth0, PROMISC true) -> chIN :: CheckIPHeader(14)[0] -> MarkIPHeader(14) -> ipINLeft -> TCPReorder(0) -> tcpINLeft -> httpINLeft -> httpOUTLeft -> tcpOUTLeft -> ipOUTLeft -> outLeft :: ToNetmapDevice(netmap:eth1);
FromNetmapDevice(netmap:eth1, PROMISC true) -> chOUT :: CheckIPHeader(14)[0] -> MarkIPHeader(14) -> ipINRight -> TCPReorder(1) -> tcpINRight -> httpINRight -> InsultRemover() -> httpOUTRight -> tcpOUTRight -> ipOUTRight -> outRight:: ToNetmapDevice(netmap:eth0);

chIN[1] -> outLeft;
chOUT[1] -> outRight;
