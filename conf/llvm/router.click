
    define ($MTU 1500)
    define ($ip 10.0.0.1)
    define ($eth b8:83:03:6f:43:40)
elementclass FNT {
    tol :: Discard(); //ToHost normally

input[0] ->
            c0 :: Classifier(    12/0806 20/0001,
                                 12/0806 20/0002,
                                 12/0800,
                                 -);

        // Respond to ARP Query
        c0[0] -> arpress :: ARPResponder($ip $eth);
        arpress[0] -> Print("ARP QUERY") -> [0]output;

        // Deliver ARP responses to ARP queriers as well as Linux.
        t :: Tee(2);
        c0[1] -> t;
        t[0] -> Print("Input to linux") -> tol; //To linux
        t[1] -> Print("Arp response") -> [0]output; //Directly output


        // Unknown ethernet type numbers.
        c0[3] -> Print() -> Discard();


    // An "ARP querier" for each interface.
    arpq0 :: EtherEncap(0x0800, b8:83:03:6f:43:40, 50:6b:4b:43:8a:da);
    // Connect ARP outputs to the interface queues.
    arpq0 -> [0]output;

    // IP routing table.
    rt :: LookupIPRouteMP(   0.0.0.0/0 0);

    // Hand incoming IP packets to the routing table.
    // CheckIPHeader checks all the lengths and length fields
    // for sanity.
    ip ::

    Strip(14)
    -> CheckIPHeader(CHECKSUM false, VERBOSE false)
    -> [0]rt;

    oerror :: IPPrint("ICMP Error : DF") -> [0]rt;
    ttlerror :: IPPrint("ICMP Error : TTL") -> [0]rt;
    //rederror :: IPPrint("ICMP Error : Redirect") -> [0]rt;


    c0[2] -> Paint(1) -> ip;
    rt[0] -> output0 :: IPOutputCombo(2, 10.1.0.1, $MTU);
    // DecIPTTL[1] emits packets with expired TTLs.
    // Reply with ICMPs. Rate-limit them?
    output0[3] -> ICMPError(10.1.0.1, timeexceeded, SET_FIX_ANNO 0) -> ttlerror;
    // Send back ICMP UNREACH/NEEDFRAG messages on big packets with DF set.
    // This makes path mtu discovery work.
    output0[4] -> ICMPError(10.1.0.1, unreachable, needfrag, SET_FIX_ANNO 0) -> oerror;
    // Send back ICMP Parameter Problem messages for badly formed
    // IP options. Should set the code to point to the
    // bad byte, but that's too hard.
    output0[2] -> ICMPError(10.1.0.1, parameterproblem, SET_FIX_ANNO 0) -> oerror;
    // Send back an ICMP redirect if required.
    output0[1] -> ICMPError(10.1.0.1, redirect, host, SET_FIX_ANNO 0) -> IPPrint("ICMP Error : Redirect") -> arpq0;
    output0[0] -> arpq0;

}

fd0 :: FromDPDKDevice(0, MAXTHREADS 1 , BURST 32 , TIMESTAMP false)
//    -> SetCycleCount()
    -> FNT()
//    -> accum :: CycleCountAccum()
    -> ToDPDKDevice(0)

DriverManager(  wait,
  //              print "RESULT-CYCLEPP $(accum.cycles_pp)",
                //read fd0.xstats,
                )
