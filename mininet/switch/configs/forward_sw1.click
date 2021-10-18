elementclass Input { $port |
    input
	-> c :: Classifier(12/86dd 54/87, 12/86DD,12/0800, 12/0806,-);

	c[0]
	-> IP6NDAdvertiser(babe:1::6 0:0:0:0:0:2) -> [1]output;

	c[1]
	-> Print("IP6 $port")
	-> Strip(14)
	-> CheckIP6Header()
	-> DecIP6HLIM()
	-> IP6Print("IP6 from port $port")

	-> Print(BENCAP, -1)
	-> IP6SREncap(ADDR babe:2::1, ADDR fc00::9, ADDR fc00::a)
	-> MarkIP6Header()
	-> Print(ENCAPED, -1)
	-> IP6Print(IPENCAPED)
	-> EtherEncap(0x86DD, SRC 0:0:0:0:0:2, DST 0:0:0:0:0:3)	
	-> output;

    c[2] -> Print("IPv4")
	-> Strip(14)
	-> CheckIPHeader()
	-> IPPrint()
	-> Discard;

    c[3] -> Print("ARP")
	-> Discard;

    c[4] -> Print("Non-IPv6") -> Discard;
}

fd1  :: FromDevice(sw1-eth0, SNIFFER false);
td1  :: ToDevice(sw1-eth1);
fd1 -> in1 :: Input(1) -> q1 :: Queue -> td1 ;

fd2  :: FromDevice(sw1-eth1, SNIFFER false);
td2  :: ToDevice(sw1-eth0);
fd2 -> in2 :: Input(2) -> q2 :: Queue -> td2 ;

in1[1] -> q2;
in2[1] -> q1;
