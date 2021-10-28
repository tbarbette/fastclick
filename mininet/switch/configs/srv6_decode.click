elementclass Input { $port, $MAC_DST |
    input
	-> c :: Classifier(12/86DD,-)
	-> Strip(14)
	-> CheckIP6Header()
	//-> IP6Print("IP6 from port $port")
	
	-> c1 :: Classifier(24/FC000000000000000000000000000009,-)
	-> c2 :: Classifier(6/2B,-)
	-> IP6SRProcess()
	-> IP6Drop(P 0.05, R 0.8, ADDR fc00::9, ADDR babe:2::5)	
	-> IP6SRv6FECDecode(DEC fc00::9, ENC fc00::a)
	-> eth :: EtherEncap(0x86DD, SRC 0:0:0:0:0:3, DST $MAC_DST)	
	-> output;

    c[1] -> Print("Non-IPv6") -> Discard;
	c1[1] -> eth -> output;
	c2[1] -> Print("Endhost packet without SRv6") -> Discard;
}

fd1  :: FromDevice(sw2-eth1, SNIFFER false);
td1  :: ToDevice(sw2-eth0);
fd1 -> Input(1, 0:0:0:0:0:4) -> Queue -> td1 ; 

fd2  :: FromDevice(sw2-eth0, SNIFFER false);
td2  :: ToDevice(sw2-eth1);
fd2 -> Input(2, 0:0:0:0:0:2) -> Queue -> td2 ; 
