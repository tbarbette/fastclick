fd1  :: FromDevice(sw1-eth0, SNIFFER false);
td1  :: ToDevice(sw1-eth1);

fd2  :: FromDevice(sw1-eth1, SNIFFER false);
td2  :: ToDevice(sw1-eth0);

fd1 -> c1 :: Classifier(12/Ox86dd, -);
// Forward IPv6 packets
c1[0] -> Strip(14) -> CheckIP6Header -> IP6Print -> td1;
// Print packet and Forward
c1[1] -> Print -> Queue -> td1;

fd2 -> c2 :: Classifier(12/Ox86dd, -);
// Forward IPv6 packets
c2[0] -> Strip(14) -> CheckIP6Header -> IP6Print -> td2;
// Print packet and Forward
c2[1] -> Print -> Queue -> td2;