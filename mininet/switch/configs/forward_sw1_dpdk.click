require(library common.click)
define( $intif 0,
        $extif 1,
        $nofec 0,
        $noencap 0,
        $doprint 0,
	$nofakefec 1);

//From internal to external
fd1  :: FromDPDKDevice($intif, PROMISC true, MTU 1610);
td1  :: ToDPDKDevice($extif);
fd1
    -> Print(INT, -1, ACTIVE $doprint) 
    -> in1 :: InputEncap($intif, 0:0:0:0:0:12, 0:0:0:0:0:13, babe:1::6, $noencap)
      -> IP6Print(INT-IP6, ACTIVE $doprint)
//    -> IP6SRProcess()
    -> senc :: {
        [0] -> s :: Switch($nofec);
            s[0] -> enc :: IP6SRv6FECEncode(ENC fc00::a, DEC fc00::9, REPAIR $nofakefec) -> [0];
            s[1] -> IP6SRDecap(FORCE_DECAP false) -> [0];
	    input[1] -> [1]enc;
    }
//    -> IP6Print(Afterprocess)
    -> o :: Output(0:0:0:0:0:12, 0:0:0:0:0:13)
    -> td1 ;

Idle -> [1]senc;

//From external to internal
fd2  :: FromDPDKDevice($extif, PROMISC true, MTU 1610);
td2  :: ToDPDKDevice($intif);
fd2 
    -> Print(EXT, -1, ACTIVE $doprint)
    -> in2 :: InputDecap($extif, 0:0:0:0:0:2, 0:0:0:0:0:1, babe:3::1)
//	-> IP6Print(EXT-IP6, ACTIVE $doprint)
  -> {
        [0] -> s :: Switch($noencap);  
 		s[0] -> IP6SRDecap(FORCE_DECAP true) -> [0];
		s[1] -> [0];
    }
//->IP6Print(DECAPED)
    -> Output(0:0:0:0:0:2, 0:0:0:0:0:1)
    -> td2 ;

in1[1] -> td2;
in2[1] -> td1;
