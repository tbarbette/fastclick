require(library common.click)
define( $intif sw1-eth0,
        $extif sw1-eth1,
        $nofec 0,
        $noencap 0);

//From internal to external
fd1  :: FromDevice($intif, SNIFFER false, PROMISC true);
td1  :: ToDevice($extif);
fd1
    //-> Print(INT) 
    -> in1 :: InputEncap($intif, 0:0:0:0:0:12, 0:0:0:0:0:13, babe:1::6, $noencap)
    //-> IP6Print(Beforeprocess)
    -> IP6SRDecap(FORCE_DECAP false) 
    -> {
        [0] -> s :: Switch($nofec);
            s[0] -> IP6SRv6FECEncode(ENC fc00::a, DEC fc00::9) -> [0];
            s[1] -> [0];
    }
    -> IP6Print(Afterprocess)
    -> o :: Output(0:0:0:0:0:12, 0:0:0:0:0:13)
    -> q1 :: Queue -> td1 ;

//From external to internal
fd2  :: FromDevice($extif, SNIFFER false, PROMISC true);
td2  :: ToDevice($intif);
fd2 
//    -> Print(EXT)
    -> in2 :: InputDecap($extif, 0:0:0:0:0:2, 0:0:0:0:0:1, babe:3::1)
//	-> IP6Print(RECEIVED)
    -> IP6SRDecap(FORCE_DECAP true)
//->IP6Print(DECAPED)
    -> Output(0:0:0:0:0:2, 0:0:0:0:0:1)
    -> q2 :: Queue -> td2 ;

in1[1] -> q2;
in2[1] -> q1;
