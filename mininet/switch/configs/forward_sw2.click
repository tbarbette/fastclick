require(library common.click)

define( $intif sw2-eth0,
        $extif sw2-eth1);

//From internal to external
fd1  :: FromDevice($intif, SNIFFER false, PROMISC true);
td1  :: ToDevice($extif);
fd1 -> in1 :: InputEncap($intif, 0:0:0:0:0:13, 0:0:0:0:0:12, babe:2::8)
    -> IP6Print(ENCAPED)
    -> Output(0:0:0:0:0:13, 0:0:0:0:0:12)
    -> q1 :: Queue -> td1 ;

//From external to internal
fd2  :: FromDevice($extif, SNIFFER false, PROMISC true);
td2  :: ToDevice($intif);
fd2 -> in2 :: InputDecap($extif, 0:0:0:0:0:3, 0:0:0:0:0:4, babe:3::2)
    -> IP6Print("Before")
	-> IP6SRProcess()
    -> IP6Print("Process")
    -> IP6SRv6FECDecode(DEC fc00::9)
    -> Print(AFTER, -1)
    -> IP6Print("After")
    -> IP6SRDecap(FORCE_DECAP true)
    -> IP6Print("Decaped")
    -> Output(0:0:0:0:0:3, 0:0:0:0:0:4)
    -> q2 :: Queue -> td2 ;

in1[1] -> q2;
in2[1] -> q1;
