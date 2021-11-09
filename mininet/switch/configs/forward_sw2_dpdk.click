require(library common.click)

define( $intif 0,
        $extif 1,
        $nofec 0,
        $noencap 0,
	$doprint 0,
	$nofakefec 0);

//From internal to external
fd1  :: FromDPDKDevice($intif, PROMISC true, MTU 1610);
td1  :: ToDPDKDevice($extif);
fd1 
    -> Print(INT, -1, ACTIVE $doprint)
    -> in1 :: InputEncap($intif, 0:0:0:0:0:13, 0:0:0:0:0:12, babe:2::8, $noencap)
    -> IP6Print(INT-IP6, ACTIVE $doprint)
    -> Output(0:0:0:0:0:13, 0:0:0:0:0:12)
    -> td1 ;

//From external to internal
fd2  :: FromDPDKDevice($extif, PROMISC true, MTU 1610);
td2  :: ToDPDKDevice($intif);
fd2 
    -> Print(EXT, -1, ACTIVE $doprint)
    -> in2 :: InputDecap($extif, 0:0:0:0:0:3, 0:0:0:0:0:4, babe:3::2)
    -> IP6Print("EXT-IP6", ACTIVE $doprint)
//    -> IP6Print("Process")
    -> {
        [0] -> s :: Switch($noencap);  
 		s[0] -> IP6SRProcess() -> [0];
		s[1] -> [0];
    }
    -> sdec :: {
        [0] -> s :: Switch($nofec);
            s[0] -> dec :: IP6SRv6FECDecode(ENC fc00::a, DEC fc00::9, RECOVER $nofakefec) -> [0];
            s[1] -> [0];
	dec[1] -> [1];
    }
//    -> Print(AFTER, -1)
//    -> IP6Print("After")
 -> {
        [0] -> s :: Switch($noencap);  
 		s[0] -> IP6SRDecap(FORCE_DECAP true) -> [0];
		s[1] -> [0];
    }
//    -> IP6Print("Decaped")
    -> Output(0:0:0:0:0:3, 0:0:0:0:0:4)
    -> td2 ;

sdec[1] -> td2;

in1[1] -> td2;
in2[1] -> td1;
