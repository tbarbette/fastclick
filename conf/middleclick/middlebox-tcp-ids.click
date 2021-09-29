/* Mac, ips and network of the MB, that acts as a router in this configuration (it must be the gateway)
 * check around line 95 to be a transparent ARP proxy instead.
 * In this example, clients are on the 10.220/16 network, and servers on the 10.221/16 network.
 * We have no load balancer in this configuration (though that is supported) so client must query
 * the address of the server on the 10.221/16 network. If the gateway of the clients is set correctly
 * (eg ip route add 10.221.0.0/16 via 10.220.0.1), the ARP resolution will lead to the packets being sent to
 * the LB. Perhaps the easisest to understand is to look in mininet/topology.py, and check the Mininet
 * network.
 **/
define($MAC1 98:03:9b:33:fe:e2) //Mac address of the port facing clients
define($MAC2 98:03:9b:33:fe:db) //Mac address of the port facing servers
define($NET1 10.220.0.0/16)
define($NET2 10.221.0.0/16)
define($IP1 10.220.0.1)
define($IP2 10.221.0.1)
define($PORT1 0) //DPDK port index, can be a PCIe address, but not an ifname
define($PORT2 1)

//IDS parameters
//Not the IDS runs on both paths. You may not want this.
define($word ATTACK) // will look for the word "ATTACK"
define($mode ALERT) /* just raise an alert. Valid values are :
 * ALERT : print an alert, does not change anything. Set readonly below to 1 for performance, as well as tcpchecksum to 0
 * MASK : Mask the text with asterisks
 * CLOSE : Close the connection if the pattern is found
 * REMOVE : Remove the word, HTTP must be enabled!!! (else the client will wait for the missing bytes). Uncomment the lines at the bottom of the file.
 * REPLACE : Replace with some text. If the text length is different, you need HTTP mode too!!
 * FULL : Remove the pattern, and add the full content of "$pattern" see below. This need the HTTPIn element to be in buffering mode, as adding bytes need to change the content length.
 */
define($all 0) //Search for multiple occurences. Not optimized.
define($pattern DELETED)

//Stack Parameters
define($inreorder 1) //Enable reordering
define($readonly 0) //Read-only (payload is never modified)
define($tcpchecksum 1) //Fix tcp checksum
define($checksumoffload 1) //Enable hardware checksum offload

//IO paramter
define($bout 32)
define($ignore 0)

//Debug parameters
define($rxverbose 99)
define($txverbose 99)
define($printarp 0)
define($printunknown 0)



//TSCClock allows fast user-level timing
TSCClock(NOWAIT true);

//JiffieClock use a counter for jiffies instead of reading the time (much like Linux)
JiffieClock(MINPRECISION 1000);

//ARPDispatcher separate ARP queries, ARP responses, and other IP packets
elementclass ARPDispatcher {
        input[0]->
                iparp :: CTXDispatcher(
                        12/0800,
                        12/0806,
                        -)
                iparp[0] -> [0]output
                iparp[1] -> arptype ::CTXDispatcher(20/0001, 20/0002, -)
                iparp[2] -> [3]output

                arptype[0] -> [1]output
                arptype[1] -> [2]output
                arptype[2] -> [3]output
}

tab :: ARPTable


/**
 * Receiver encapsulate everything to handle one port (CTXManager, ARP management, IP checker, ...
 *
 */
elementclass Receiver { $port, $mac, $ip, $range |
    input[0]
    -> arpq :: ARPQuerier($ip, $mac, TABLE tab)
    -> etherOUT :: Null

    f :: FromDPDKDevice($port, VERBOSE $rxverbose, PROMISC false, RSS_AGGREGATE 1, THREADOFFSET 0, MAXTHREADS 1)
    -> fc :: CTXManager(BUILDER 1, AGGCACHE false, CACHESIZE 65536, VERBOSE 1, EARLYDROP true)
    -> arpr :: ARPDispatcher()

    arpr[0]
    -> FlowStrip(14)
    -> receivercheck :: CheckIPHeader(CHECKSUM false)
    -> inc :: CTXDispatcher(9/01 0, 9/06 0, -)


    inc[0] //TCP or ICMP
    -> [0]output;


    inc[1]
    -> IPPrint("UNKNOWN IP")
    -> Unstrip(14)
        -> Discard

    arpr[1]
    -> Print("RX ARP Request $mac", -1, ACTIVE $printarp)
    -> arpRespIN :: ARPResponder($range $mac)
    -> Print("TX ARP Responding", -1, ACTIVE $printarp)
    -> etherOUT;

    arpRespIN[1] -> Print("ARP Packet not for $mac", -1) -> Discard

    arpr[2]
    -> Print("RX ARP Response $mac", -1, ACTIVE $printarp)
    -> [1]arpq;

    arpr[3] -> Print("Unknown packet type IN???",-1, ACTIVE $printunknown) -> Discard();

    etherOUT
    -> t :: ToDPDKDevice($port,BLOCKING true,BURST $bout, ALLOC true, VERBOSE $txverbose, TCO $checksumoffload)
}

//Replace the second IP1 per NET1 to act as a transparent ARP proxy, therefore not routing
r1 :: Receiver($PORT1,$MAC1,$IP1,$IP1);
r2 :: Receiver($PORT2,$MAC2,$IP2,$IP2);

//Idle -> host :: Null;

r1
  ->  up ::
  { [0]
    -> IPIn
    -> tIN :: TCPIn(FLOWDIRECTION 0, OUTNAME up/tOUT, RETURNNAME down/tIN, REORDER $inreorder)

    //HTTPIn, uncomment when needed (see above)
    //-> HTTPIn(HTTP10 false, NOENC false, BUFFER 0)
    -> wm :: WordMatcher(WORD $word, MODE $mode, ALL $all, QUIET false, MSG $pattern)
    //Same than IN
    //-> HTTPOut()
    -> tOUT :: TCPOut(READONLY $readonly, CHECKSUM $tcpchecksum)
    -> IPOut(READONLY $readonly, CHECKSUM false)
    -> [0]
  }
  -> r2;

r2
  -> down ::
  { [0]
    -> IPIn
    -> tIN :: TCPIn(FLOWDIRECTION 1, OUTNAME down/tOUT, RETURNNAME up/tIN, REORDER $inreorder)
    //-> HTTPIn(HTTP10 false, NOENC false, BUFFER 0)
    -> wm :: WordMatcher(WORD $word, MODE $mode, ALL $all, QUIET false, MSG $pattern)
    //-> HTTPOut()
    -> tOUT :: TCPOut(READONLY $readonly, CHECKSUM $tcpchecksum)
    -> IPOut(READONLY $readonly, CHECKSUM false)
    -> [0]
  }
  -> r1;
