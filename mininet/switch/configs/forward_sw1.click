elementclass Input { $port |
    input
	-> c :: Classifier(12/86DD,-)
	-> Strip(14)
	-> CheckIP6Header()
	-> IP6Print("IP6 from port $port")

	-> Print(BENCAP, -1)
	-> IP6SREncap(ADDR babe:2::1, ADDR fc00::9, ADDR fc00::a)
	-> Print(ENCAPED, -1)
	-> IPPrint(IPENCAPED)
	-> EtherEncap(0x86DD, SRC 0:0:0:0:0:2, DST 0:0:0:0:0:3)	
	-> output;

    c[1] -> Print("Non-IPv6") -> Discard;
}

fd1  :: FromDevice(sw1-eth0, SNIFFER false);
td1  :: ToDevice(sw1-eth1);
fd1 -> Input(1) -> Queue -> td1 ; 

fd2  :: FromDevice(sw1-eth1, SNIFFER false);
td2  :: ToDevice(sw1-eth0);
fd2 -> Input(2) -> Queue -> td2 ; 
