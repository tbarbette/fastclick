elementclass Input { $port, $MAC_DST |
    input
	-> c :: Classifier(12/86DD,-)
	-> Strip(14)
	-> CheckIP6Header()
	-> IP6Print("IP6 from port $port")
	
	-> c1 :: Classifier(24/FC00000000000000000000000000000A,-)
	-> c2 :: Classifier(6/2B,-)
	-> IP6SRProcess()
	
	//-> IP6SRv6FECEncode(ENC fc00::a, DEC fc00::9)
	-> eth :: EtherEncap(0x86DD, SRC 0:0:0:0:0:2, DST $MAC_DST)	
	-> output;

    c[1] -> Print("Non-IPv6") -> Discard;
	c1[1] -> Print("Transit packet") -> eth -> output;
	c2[1] -> Print("Endhost packet without SRv6") -> Discard;
}

fd1  :: FromDevice(sw1-eth0, SNIFFER false);
td1  :: ToDevice(sw1-eth1);
fd1 -> Input(1, 0:0:0:0:0:3) -> Queue -> td1 ; 

fd2  :: FromDevice(sw1-eth1, SNIFFER false);
td2  :: ToDevice(sw1-eth0);
fd2 -> Input(2, 0:0:0:0:0:1) -> Queue -> td2 ; 
