ipINLeft :: IPIn();
tcpINLeft :: TCPIn(tcpOUTLeft, tcpINRight);
httpINLeft :: HTTPIn();
ipOUTLeft :: IPOut();
tcpOUTLeft :: TCPOut();
httpOUTLeft :: HTTPOut();

ipINRight :: IPIn();
tcpINRight :: TCPIn(tcpOUTRight, tcpINLeft);
httpINRight :: HTTPIn();
ipOUTRight :: IPOut();
tcpOUTRight :: TCPOut();
httpOUTRight :: HTTPOut();

FromNetmapDevice(netmap:eth0, PROMISC true) -> Strip(14) -> chIN :: CheckIPHeader()[0] -> ipINLeft -> TCPReorder() -> tcpINLeft -> httpINLeft -> httpOUTLeft -> tcpOUTLeft -> ipOUTLeft -> outLeft :: Unstrip(14) -> ToNetmapDevice(netmap:eth1);
FromNetmapDevice(netmap:eth1, PROMISC true) -> Strip(14) -> chOUT :: CheckIPHeader()[0] -> ipINRight -> TCPReorder() -> tcpINRight -> httpINRight -> InsultRemover() -> httpOUTRight -> tcpOUTRight -> ipOUTRight -> outRight :: Unstrip(14) -> ToNetmapDevice(netmap:eth0);

chIN[1] -> outLeft;
chOUT[1] -> outRight;
