/* 
 * This file implements a fast L3 UDP packet generatior
 *
 * This can be used to test the throughput of a DUT, using a receiver-l3.click
 * on some other end.
 *
 *
 * A launch line would be :
 *   sudo bin/click -c 0x1 -n 4 -- conf/fastclick/pktgen-l3.click L=60 S=1000000 N=100
 */

/*You do not need to change the mac address as we run in promisc, but you need
 to set the srcip, gateway ip and dstip correctly */
define($smac 90:e2:ba:c3:79:66)
define($dmac 00:00:00:00:00:00)
define($srcip 192.168.130.4)
define($dstip 192.168.128.13)
define($gatewayip 192.168.130.1)

//Explained in loop.click
define($verbose 3)
define($blocking true)

//###################
// TX
//###################
//Create a UDP flow of $N packets
FastUDPFlows(RATE 0, LIMIT $N, LENGTH $L, SRCETH $smac, DSTETH $dmac, SRCIP $srcip, DSTIP $dstip, FLOWS 1, FLOWSIZE $N)
-> MarkMACHeader
//EnsureDPDKBuffer will copy the packet inside a DPDK buffer, so there is no more copies (not even to the NIC) afterwards when we replay the packet many time
-> EnsureDPDKBuffer
-> Strip(14)
-> CheckIPHeader
-> SetIPAddress($gatewayip)
-> uq :: Unqueue()
-> arp_q :: ARPQuerier($srcip, $smac)
-> Queue
//MutliReplayUqueue pulls all packets from its input, and replay them from memory $S amount of time
-> replay :: MultiReplayUnqueue(STOP -1, ACTIVE false, QUICK_CLONE 0)
-> ic0 :: AverageCounter()
-> td :: ToDPDKDevice(0, BLOCKING $blocking, VERBOSE $verbose)

//Do not replay arp queries...
arp_q[1]
-> td

//It is good practice to pin any source to let FastClick know what will eat the CPU and allocate FromDPDKDevice threads accordingly. It also help you know what you're doing. Multithreading is everything but magic.
StaticThreadSched(replay 0)

//###################
// RX
//###################
fd1 :: FromDPDKDevice(0, PROMISC true, VERBOSE $verbose)
-> arp_c :: Classifier(12/0800, 12/0806 20/0001, 12/0806 20/0002, -)
arp_c[0] -> Print("IP") -> Discard
arp_c[1] -> arp_r :: ARPResponder($srcip $smac) -> td
arp_c[2] 
-> [1]arp_q
arp_c[3] -> Print("Other") -> Discard

Script(  TYPE ACTIVE,
                wait 2s, //Let time for ARP
				write replay.active true, //Launch replay
				label start,
				wait 1s,
				print "Number of packets sent : $(ic0.count)",
				print "TX link rate : $(ic0.link_rate)", 
				goto start
				)
