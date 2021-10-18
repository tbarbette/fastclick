require(library common.click)

//From internal to external
fd1  :: FromDevice(sw1-eth0, SNIFFER false);
td1  :: ToDevice(sw1-eth1);
fd1 -> in1 :: InputEncap(eth0, 0:0:0:0:0:12, 0:0:0:0:0:13, babe:1::6) -> q1 :: Queue -> td1 ;

//From external to internal
fd2  :: FromDevice(sw1-eth1, SNIFFER false);
td2  :: ToDevice(sw1-eth0);
fd2 -> in2 :: InputDecap(eth1, 0:0:0:0:0:2, 0:0:0:0:0:1, babe:3::1) -> q2 :: Queue -> td2 ;

in1[1] -> q2;
in2[1] -> q1;
