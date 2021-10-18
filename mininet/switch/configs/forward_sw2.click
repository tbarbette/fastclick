elementclass Input { $port |
    input
	-> c :: Classifier(12/86DD,-)
	-> Strip(14)
	-> CheckIP6Header()
	-> DecIP6HLIM()
	-> IP6Print("IP6 from port $port")
	-> IP6SRDecap()
	-> Print(DECAPED, -1)
	-> MarkIP6Header
	-> IP6Print(IPDECAPED)
	-> EtherEncap(0x86DD, SRC 0:0:0:0:0:3, DST 0:0:0:0:0:4)
	-> output;

    c[1] -> Print("Non-IPv6") -> Discard;
}

fd1  :: FromDevice(sw2-eth0, SNIFFER false);
td1  :: ToDevice(sw2-eth1);
fd1 -> Input(1) -> Queue -> td1 ; 

fd2  :: FromDevice(sw2-eth1, SNIFFER false);
td2  :: ToDevice(sw2-eth0);
fd2 -> Input(2) -> Queue -> td2 ; 
