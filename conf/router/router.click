/*
 *L3 router but replacing ARP elements by static ones.
 *
 *It supports any number of cores and use the amount of queue needed to
 * serve them. All given cores (with -j parameter) will be used in the best
 * way, therefore you do not need to use StaticThreadSched elements.
 *
 *Note that Netmap do not allow to change the number of queues
 * programmaticaly as opposed to DPDK. With DPDK, if you give 12 cores to
 * use with 4 devices, FastClick will configure 3 queues per device by
 * itself, but you need to use "ethtool -L combined 3" with netmap yourself.
 *
 * In all situations, never let the default amount of queue of your system
 * (probably equal to the number of CPU) anyway as it is way too much and force
 * Click (or your Linux stack) to check for all queues in loop as RSS could
 * push packet to all RX queues and has a hard cost on memory locality.
 */

// Interfaces
// eth2, 90:e2:ba:46:f2:d4, 10.1.0.1
// eth3, 90:e2:ba:46:f2:d5, 10.2.0.1
// eth4, 90:e2:ba:46:f2:e0, 10.3.0.1
// eth5, 90:e2:ba:46:f2:e1, 10.4.0.1

define ($MTU 1500)
define ($bout 64)
define ($bin 64)
define ($i 1024)
tol :: Discard(); //ToHost normally

elementclass Input { $device,$ip,$eth |

    FromNetmapDevice($device, BURST $bin) -> 
    
    c0 :: Classifier(    12/0806 20/0001,
                         12/0806 20/0002,
                         12/0800,
                         -);
                          
    // Respond to ARP Query
    c0[0] -> arpress :: ARPResponder($ip $eth);
    arpress[0] -> Print("ARP QUERY") -> [1]output;
                    
    // Deliver ARP responses to ARP queriers as well as Linux.
    t :: Tee(2);
    c0[1] -> t;
    t[0] -> Print("Input to linux") -> [2]output; //To linux
    t[1] -> Print("Arp response") -> [1]output; //Directly output
    
    //Normal IP to output 0
    c0[2] -> [0]output;
    
    // Unknown ethernet type numbers.
    c0[3] -> Print() -> Discard();
    
}


teth2 :: ToNetmapDevice(netmap:eth2 , BURST $bout, IQUEUE $i, BLOCKING true)
teth3 :: ToNetmapDevice(netmap:eth3 , BURST $bout, IQUEUE $i, BLOCKING true)
teth4 :: ToNetmapDevice(netmap:eth4 , BURST $bout, IQUEUE $i, BLOCKING true)
teth5 :: ToNetmapDevice(netmap:eth5 , BURST $bout, IQUEUE $i, BLOCKING true)

input0 :: Input(netmap:eth2, 10.1.0.1, 90:e2:ba:46:f2:d4);
input1 :: Input(netmap:eth3, 10.2.0.1, 90:e2:ba:46:f2:d5);
input2 :: Input(netmap:eth4, 10.3.0.1, 90:e2:ba:46:f2:e0);
input3 :: Input(netmap:eth5, 10.4.0.1, 90:e2:ba:46:f2:e1);

input0[1] -> teth2;
input1[1] -> teth3;
input2[1] -> teth4;
input3[1] -> teth5;

input0[2] -> tol;
input1[2] -> tol;
input2[2] -> tol;
input3[2] -> tol;


// An "ARP querier" for each interface.
arpq0 :: EtherEncap(0x0800, 90:e2:ba:46:f2:d4, 00:00:00:11:11:11);
arpq1 :: EtherEncap(0x0800, 90:e2:ba:46:f2:d5, 00:00:00:22:22:22);
arpq2 :: EtherEncap(0x0800, 90:e2:ba:46:f2:e0, 00:00:00:33:33:33);
arpq3 :: EtherEncap(0x0800, 90:e2:ba:46:f2:e1, 00:00:00:44:44:44);

// Connect ARP outputs to the interface queues.
arpq0 -> teth2;
arpq1 -> teth3;
arpq2 -> teth4;
arpq3 -> teth5;


// IP routing table.
rt :: LookupIPRouteMP(   10.1.0.1/16 0,
                         10.2.0.1/16 1,
                         10.3.0.1/16 2,
                         10.4.0.1/16 3);

// Hand incoming IP packets to the routing table.
// CheckIPHeader checks all the lengths and length fields
// for sanity.
ip :: 

Strip(14)
-> CheckIPHeader(INTERFACES 10.1.0.1/16 10.2.0.1/16 10.3.0.1/16 10.4.0.1/16, VERBOSE true)
-> [0]rt;

oerror :: IPPrint("ICMP Error : DF") -> [0]rt;
ttlerror :: IPPrint("ICMP Error : TTL") -> [0]rt;
//rederror :: IPPrint("ICMP Error : Redirect") -> [0]rt;


input0[0] -> Paint(1) -> ip;
input1[0] -> Paint(2) -> ip;
input2[0] -> Paint(3) -> ip;
input3[0] -> Paint(4) -> ip;

// IP packets for this machine.
// ToHost expects ethernet packets, so cook up a fake header.
//rt[4] -> EtherEncap(0x0800, 1:1:1:1:1:1, 2:2:2:2:2:2) -> tol;

rt[0] -> output0 :: IPOutputCombo(1, 10.1.0.1, $MTU);
rt[1] -> output1 :: IPOutputCombo(2, 10.2.0.1, $MTU);
rt[2] -> output2 :: IPOutputCombo(3, 10.3.0.1, $MTU);
rt[3] -> output3 :: IPOutputCombo(4, 10.4.0.1, $MTU);

// DecIPTTL[1] emits packets with expired TTLs.
// Reply with ICMPs. Rate-limit them?
output0[3] -> ICMPError(10.1.0.1, timeexceeded, SET_FIX_ANNO 0) -> ttlerror;
output1[3] -> ICMPError(10.2.0.1, timeexceeded, SET_FIX_ANNO 0) -> ttlerror;
output2[3] -> ICMPError(10.3.0.1, timeexceeded, SET_FIX_ANNO 0) -> ttlerror;
output3[3] -> ICMPError(10.4.0.1, timeexceeded, SET_FIX_ANNO 0) -> ttlerror;

// Send back ICMP UNREACH/NEEDFRAG messages on big packets with DF set.
// This makes path mtu discovery work.
output0[4] -> ICMPError(10.1.0.1, unreachable, needfrag, SET_FIX_ANNO 0) -> oerror;
output1[4] -> ICMPError(10.2.0.1, unreachable, needfrag, SET_FIX_ANNO 0) -> oerror;
output2[4] -> ICMPError(10.3.0.1, unreachable, needfrag, SET_FIX_ANNO 0) -> oerror;
output3[4] -> ICMPError(10.4.0.1, unreachable, needfrag, SET_FIX_ANNO 0) -> oerror;

// Send back ICMP Parameter Problem messages for badly formed
// IP options. Should set the code to point to the
// bad byte, but that's too hard.
output0[2] -> ICMPError(10.1.0.1, parameterproblem, SET_FIX_ANNO 0) -> oerror;
output1[2] -> ICMPError(10.2.0.1, parameterproblem, SET_FIX_ANNO 0) -> oerror;
output2[2] -> ICMPError(10.3.0.1, parameterproblem, SET_FIX_ANNO 0) -> oerror;
output3[2] -> ICMPError(10.4.0.1, parameterproblem, SET_FIX_ANNO 0) -> oerror;

// Send back an ICMP redirect if required.
output0[1] -> ICMPError(10.1.0.1, redirect, host, SET_FIX_ANNO 0) -> IPPrint("ICMP Error : Redirect") -> arpq0;
output1[1] -> ICMPError(10.2.0.1, redirect, host, SET_FIX_ANNO 0) -> IPPrint("ICMP Error : Redirect") -> arpq1;
output2[1] -> ICMPError(10.3.0.1, redirect, host, SET_FIX_ANNO 0) -> IPPrint("ICMP Error : Redirect") -> arpq2;
output3[1] -> ICMPError(10.4.0.1, redirect, host, SET_FIX_ANNO 0) -> IPPrint("ICMP Error : Redirect") -> arpq3;

output0[0] -> arpq0;
output1[0] -> arpq1;
output2[0] -> arpq2;
output3[0] -> arpq3;
