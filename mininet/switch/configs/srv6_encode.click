elementclass Input { $port |
    input
	-> c :: Classifier(12/86DD,-)
	-> Strip(14)
	-> CheckIP6Header()
	
	-> c1 :: Classifier(24/FC00000000000000000000000000000A,24/FC00000000000000000000000000000B,-);
	
	c1[0] -> [0]output;
	c1[1] -> [1]output;
	c1[2] -> [2]output;

	c[1] -> Discard;
};

fec  :: IP6SRv6FECEncode(ENC fc00::a, DEC fc00::9, WINDOW 10, STEP 2, SCHEME 0);

fd1  :: FromDevice(sw1-eth0, SNIFFER false);
td1  :: ToDevice(sw1-eth1);
fd1 -> inp :: Input(1);
inp[0] -> c2 :: Classifier(6/2B,-)
	-> IP6SRProcess()
	-> [0]fec
	-> eth :: EtherEncap(0x86DD, SRC 0:0:0:0:0:2, DST 0:0:0:0:0:3)	
	-> q  :: Queue; 
inp[1] -> Print("WTF") -> Discard;
inp[2] -> eth -> q;
c2[1] -> Discard;
q -> td1;

fd2  :: FromDevice(sw1-eth1, SNIFFER false);
td2  :: ToDevice(sw1-eth0);
fd2 -> inp2 :: Input(2);
inp2[1] -> c3 :: Classifier(6/2B,-)
	-> IP6SRProcess()
	-> [1]fec;
inp2[0] -> Print("Should not") -> Discard;
inp2[2] -> eth2  :: EtherEncap(0x86DD, SRC 0:0:0:0:0:2, DST 0:0:0:0:0:1) -> q2  :: Queue;
c3[1] -> Discard;
q2 -> td2;