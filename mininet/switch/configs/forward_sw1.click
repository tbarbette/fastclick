elementclass Input { $port |
    input
	-> c :: Classifier(12/86DD,-)
	-> Strip(14)
	-> CheckIP6Header()
	-> IP6Print("IP6 from port $port")

	-> c2 :: Classifier(6/2B,-)
	-> IP6SRv6FECEncode(fc00::a, fc00::9)
	-> EtherEncap(0x86DD, SRC 0:0:0:0:0:2, DST 0:0:0:0:0:3)	
	-> output;

    c[1] -> Print("Non-IPv6") -> Discard;
	c2[1] -> Print("Non-SRv6") -> output;
}

fd1  :: FromDevice(sw1-eth0, SNIFFER false);
td1  :: ToDevice(sw1-eth1);
fd1 -> Input(1) -> Queue -> td1 ; 

fd2  :: FromDevice(sw1-eth1, SNIFFER false);
td2  :: ToDevice(sw1-eth0);
fd2 -> Input(2) -> Queue -> td2 ; 
