/* 
 * This config is the same than tester-l3 but repeats itself for every 4 packet
 * size starting at $L.
 *
 * You can grep the output for RESULT and make a nice graph out of this to see
 * how your DUT react to different packet sizes.
 *
 * A launch line would be :
 *   sudo bin/click -c 0xf -n 4 -- conf/fastclick/tester-l3-loop.click L=60 N=100 S=100000
 */

/*You do not need to change the mac address as we run in promisc, but you need
 to set the srcip, gateway ip and dstip correctly */
define($smac 90:e2:ba:c3:79:66)
define($dmac 90:e2:ba:c3:79:68)
define($srcip 192.168.130.4)
define($dstip 192.168.128.14)
define($gatewayip 192.168.130.1)

define($lanport 0000:03:00.0)
define($wanport 0000:01:00.0)

//Explained in loop.click
define($verbose 3)
define($blocking true)

//###################
// TX
//###################
//Create a UDP flow of $N packets
is :: FastUDPFlows(RATE 0, LIMIT $N, LENGTH $L, SRCETH $smac, DSTETH $dmac, SRCIP $srcip, DSTIP $dstip, FLOWS 1, FLOWSIZE $N)
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
-> replay :: MultiReplayUnqueue(STOP -1, ACTIVE false, QUICK_CLONE 1)
-> ic0 :: AverageCounter()
-> td0 :: ToDPDKDevice($lanport, BLOCKING $blocking, VERBOSE $verbose)

//Do not replay arp queries...
arp_q[1]
-> td0

//Send a small packet every second to advertise our mac src
td1 :: ToDPDKDevice($wanport, BLOCKING $blocking, VERBOSE $verbose)


//It is good practice to pin any source to let FastClick know what will eat the CPU and allocate FromDPDKDevice threads accordingly. It also help you know what you're doing. Multithreading is everything but magic.
StaticThreadSched(replay 0)

//###################
// RX
//###################
fd0 :: FromDPDKDevice($lanport, PROMISC true, VERBOSE $verbose)
-> arp_c0 :: Classifier(12/0800, 12/0806 20/0001, 12/0806 20/0002, -)
arp_c0[0] -> Print("R0 IP") -> Discard
arp_c0[1] -> Print("R0 ARPQ") ->arp_r0 :: ARPResponder($srcip $smac) -> td0
arp_c0[2] -> Print("R0 ARPR") -> [1]arp_q
arp_c0[3] -> Print("R0 Other") -> Discard

fd1 :: FromDPDKDevice($wanport, PROMISC true, VERBOSE $verbose)
-> arp_c1 :: Classifier(12/0800, 12/0806 20/0001, 12/0806 20/0002, -)
arp_c1[0] -> Print("R1 IP", ACTIVE false) -> oc0 :: AverageCounter() -> Discard
arp_c1[1] -> Print("R1 ARPQ") -> arp_r1 :: ARPResponder($dstip $dmac) -> td1
arp_c1[2] -> Discard
arp_c1[3] -> Print("R1 Other") -> Discard

DriverManager(  wait 1s, //First small round to set up ARP etc
				write replay.stop 1,
				write replay.active true,
				wait,
				wait 2s,
 				set LENGTH $L,
				label start,
				print "Launching test with L=$LENGTH",
				write oc0.reset,
				write ic0.reset,
				write is.length $LENGTH,
				write is.reset,
				set REP $(mul $(idiv $S $LENGTH) 60),
				write replay.stop $REP,
				write replay.active true,
				write replay.reset,
				wait,
				wait 10ms, print $(ic0.count), print $(oc0.count),
				print $(ic0.link_rate), print $(oc0.link_rate),
				set oloss $(div $(sub $(ic0.link_count) $(oc0.link_count)) $(ic0.link_count)),
				print "RESULT $LENGTH $(oc0.link_rate) $oloss",
				set LENGTH $(add $LENGTH 4),
				goto start $(le $LENGTH 1500),
				print "All test finished !",
				)
