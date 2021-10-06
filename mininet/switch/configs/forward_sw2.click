elementclass Input { $port |
    input
	-> c :: Classifier(12/86DD,-)
	-> Strip(14)
	-> CheckIP6Header()
	-> IP6Print("IP6 from port $port")
	-> Unstrip(14)
	-> output;

    c[1] -> Print("Non-IPv6") -> Discard;
}

fd1  :: FromDevice(sw2-eth0, SNIFFER false);
td1  :: ToDevice(sw2-eth1);
fd1 -> Input(1) -> Queue -> td1 ; 

fd2  :: FromDevice(sw2-eth1, SNIFFER false);
td2  :: ToDevice(sw2-eth0);
fd2 -> Input(2) -> Queue -> td2 ; 
