//Ports
define($MAC1 98:03:9b:33:fe:e2)
define($MAC2 98:03:9b:33:fe:db)
define($NET1 10.220.0.0/16)
define($NET2 10.221.0.0/16)
define($IP1 10.220.0.1)
define($IP2 10.221.0.1)
define($PORT1 0)
define($PORT2 1)

//Parameters
define($rxverbose 99)
define($txverbose 99)
define($bout 32)
define($ignore 0)

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
    -> Print("RX ARP Request $mac", -1)
    -> arpRespIN :: ARPResponder($range $mac)
    -> Print("TX ARP Responding", -1)
    -> etherOUT;

    arpRespIN[1] -> Print("ARP Packet not for $mac", -1) -> Discard

    arpr[2]
    -> Print("RX ARP Response $mac", -1)
    -> [1]arpq;

    arpr[3] -> Print("Unknown packet type IN???",-1) -> Discard();

    etherOUT
    -> t :: ToDPDKDevice($port,BLOCKING true,BURST $bout, ALLOC true, VERBOSE $txverbose, TCO true)
}

r1 :: Receiver($PORT1,$MAC1,$IP1,$IP1);
r2 :: Receiver($PORT2,$MAC2,$IP2,$IP2);
//Idle -> host :: Null;

r1
//Elements on the forward path
 -> r2;

r2
//Elements on the return path
 -> r1;
