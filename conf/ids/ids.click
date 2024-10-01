/** 
 * A packet-based IDS based on the Aho-Corasick elements made by the authors
 * of the OpenBox SIGCOMM '16 paper. We modified their elements to make
 * multi-threaded version (with the MP suffix) as part of the Metron (NSDI'18 and ToN 2021) papers.
 * 
 * Check the OpenBox paper for information about the IDS elements themselves. You might want to check the alternative MiddleClick-based flow IDS that is based on HyperScan and work on stream of data (TCP stream, HTTP payload) and is therefore resistent to eviction attacks as it will also match payload accross packets.
 *
 */

require(library includes/FNT.click)

elementclass  Forwarder{ $port,$srcmac,$dstmac |
        fd :: FromDPDKDevice($port, MAC $srcmac,MINTHREADS $RCPUNR, MAXTHREADS $RCPUNR, NUMA $NUMA, VERBOSE 99)
                -> FNT()
                -> output;

        input
                -> EtherRewrite($srcmac, $dstmac)
                -> tdOUT :: ToDPDKDevice($port, BLOCKING $BLOCKING, IQUEUE $IQUEUE);

        // Just advertise L2 for the switch
        adv1 :: FastUDPFlows(RATE 0, LIMIT -1, LENGTH 64, SRCETH $srcmac, DSTETH ff:ff:ff:ff:ff:ff, SRCIP 10.99.98.97, DSTIP 10.97.98.99, FLOWS 1, FLOWSIZE 1)
                -> advq1 :: RatedUnqueue(1)
                -> tdOUT;
}

f0 :: Forwarder(${self:$NIC:pci}, ${server:$NIC:mac}, ${client:$RCV_NIC:mac});

f0->f0;

DriverManager(wait, print "RESULT-DROPPED $(add $(f0/fd.hw_dropped) $(f1/fd.hw_dropped) $(f2/fd.hw_dropped) $(f3/fd.hw_dropped))");
