require(library common.click)

//From internal to external
fd1  :: FromDevice(sw2-eth0, SNIFFER false);
td1  :: ToDevice(sw2-eth1);
fd1 -> in1 :: InputEncap(eth0, 0:0:0:0:0:13, 0:0:0:0:0:12, babe:2::8)
    -> Output(0:0:0:0:0:13, 0:0:0:0:0:12)
    -> q1 :: Queue -> td1 ;

//From external to internal
fd2  :: FromDevice(sw2-eth1, SNIFFER false);
td2  :: ToDevice(sw2-eth0);
fd2 -> in2 :: InputDecap(eth1, 0:0:0:0:0:3, 0:0:0:0:0:4, babe:3::2)
    -> IP6Print("Before")
	-> IP6SRProcess()
	-> IP6SRv6FECDecode(DEC fc00::9)
        -> IP6Print("After")
    -> IP6SRDecap(FORCE_DECAP true)
    -> IP6Print("Decaped")
    -> Output(0:0:0:0:0:3, 0:0:0:0:0:4)
    -> q2 :: Queue -> td2 ;

in1[1] -> q2;
in2[1] -> q1;