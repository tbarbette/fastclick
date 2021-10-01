fd1  :: FromDevice(sw2-eth0, SNIFFER false);
td1  :: ToDevice(sw2-eth1);
fd1 -> p1 :: Print -> Queue -> td1 ; 

fd2  :: FromDevice(sw2-eth1, SNIFFER false);
td2  :: ToDevice(sw2-eth0);
fd2 -> p2 :: Print -> Queue -> td2 ; 