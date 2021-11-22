require(library common.click)


info :: DPDKInfo()

define( $intif 0,
        $extif 1,
        $nofec 0,
        $noencap 0,
	$doprint 0,
        $dodrop 0,
	$nofakefec 0);

//From internal to external
fd1  :: FromDevice(sw2-eth1, SNIFFER false);
td1  :: ToDevice(sw2-eth0);
fd1 
    -> pr1 :: Print(INT, -1, ACTIVE $doprint)
    -> in1 :: InputEncap($intif, 0:0:0:0:0:13, 0:0:0:0:0:12, babe:2::8, $noencap)
    -> IP6Print(INT-IP6, ACTIVE $doprint)
    -> Output(0:0:0:0:0:13, 0:0:0:0:0:12)
    -> td1 ;

//From external to internal
fd2  :: FromDevice(sw2-eth0, SNIFFER false);
td2  :: ToDevice(sw2-eth1);
fd2 
    -> pr2 :: Print(EXT, -1, ACTIVE $doprint)
    -> in2 :: InputDecap($extif, 0:0:0:0:0:3, 0:0:0:0:0:4, babe:3::2)
    -> IP6Print("EXT-IP6", ACTIVE $doprint)
//    -> IP6Print("Process")
    -> {
        [0] -> s :: Switch($noencap);  
 		s[0] -> IP6SRProcess() -> [0];
		s[1] -> [0];
    }
    -> sdrop :: {
        [0] -> s :: Switch($dodrop);
            s[0] -> [0];
            s[1] -> drop :: IP6Drop(ADDR fc00::9, ADDR babe:3::2) -> [0];
    }
    -> sdec :: {
        [0] -> s :: Switch($nofec);
            s[0] -> dec :: IP6SRv6FECDecode(ENC fc00::a, DEC fc00::9, RECOVER $nofakefec) -> [0];
            s[1] -> [0];
	dec[1] -> [1];
    }
//    -> Print(AFTER, -1)
//    -> IP6Print("After")
    -> cREPAIR :: Classifier(24/FC00000000000000000000000000000A,-);
cREPAIR[1]
 -> {
        [0] -> s :: Switch($noencap);  
 		s[0] -> IP6SRDecap(FORCE_DECAP true) -> [0];
		s[1] -> [0];
    }
//    -> IP6Print("Decaped")
    -> Output(0:0:0:0:0:3, 0:0:0:0:0:4)
    -> td2 ;
cREPAIR[0] -> Discard;

sdec[1] -> td2;

in1[1] -> td2;
in2[1] -> td1;

DriverManager(wait,
		print "RESULT-DECODER_COUNT $(fd2.hw_count)",
		print "RESULT-DECODER_TXCOUNT $(td2.count)",
		)	



Script(TYPE ACTIVE,
	set time $(now),
	print "DEC-$time-RESULT-LOAD $(load)",
	wait 1s,
	loop

);
