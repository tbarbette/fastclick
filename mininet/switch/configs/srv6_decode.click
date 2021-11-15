define( $drop_h 0,
        $drop_r 0,
        $drop_k 1,
        $drop_p 0,
		$no_fec 0);

elementclass Input { $port, $MAC_DST, $MAC_DST2 |
    input
	-> c :: Classifier(12/86DD,-)
	-> Strip(14)
	-> CheckIP6Header()
	//-> IP6Print("IP6 from port $port")
	
	-> c1 :: Classifier(24/FC000000000000000000000000000009,-)
	-> c2 :: Classifier(6/2B,-)
	-> IP6SRProcess()
	// -> IP6Drop(ADDR fc00::9, ADDR babe:2::5, K $drop_k, R $drop_r, H $drop_h, P $drop_p)
	-> sFEC :: Switch($no_fec)	
	-> fec  :: IP6SRv6FECDecode(DEC fc00::9, ENC fc00::a)
	-> c3 :: Classifier(24/FC00000000000000000000000000000A,-);
	c3[1]
	-> eth :: EtherEncap(0x86DD, SRC 0:0:0:0:0:3, DST $MAC_DST)	
	-> [0]output;
	sFEC[1] -> eth;

	c3[0] -> Discard;

	fec[1] -> EtherEncap(0x86DD, SRC 0:0:0:0:0:3, DST $MAC_DST2)
	-> [1]output;

    c[1] -> Print("Non-IPv6") -> Discard;
	c1[1] -> eth -> [0]output;
	c2[1] -> Print("Endhost packet without SRv6") -> Discard;
}

fd1  :: FromDevice(sw2-eth1, SNIFFER false);
td1  :: ToDevice(sw2-eth0);
fd1 -> inp  :: Input(1, 0:0:0:0:0:4, 0:0:0:0:0:2) -> Queue -> td1 ; 

fd2  :: FromDevice(sw2-eth0, SNIFFER false);
td2  :: ToDevice(sw2-eth1);
fd2 -> inp2  :: Input(2, 0:0:0:0:0:2, 0:0:0:0:0:4) -> qback  :: Queue -> td2 ; 

inp[1] -> Print("Ouais cest ici que ca merde") -> qback;
inp2[1] -> Discard;