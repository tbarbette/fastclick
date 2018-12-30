/*
 * This file implements a fast L2 pktgen which sends UDP traffic on one NIC 
 *  towards a second NIC
 *
 * This can be used to test the throughput of a L2 switch.
 *
 * The second port is used to receive the traffic back and compute 
 * some statistics. We do not respond to ARP packets, hence the L2 limitation.
 *
 * A launch line would be :
 *   sudo bin/click -c 0x7 -n 4 -- conf/fastclick/tester-l2.click L=60 S=1000000 N=100
 */

//You do not need to change these, we send a packet with our virtual mac source before launching the pktgen so any switch can learn about us
define($macA 11:22:33:c3:77:d2)
define($macB 11:22:33:c3:77:70)

define($portA 0)
define($portB 1)

//Explained in loop.click
define($verbose 0)
define($blocking true)

//###################
// TX
//###################
//Create a UDP flow of $N packets
FastUDPFlows(RATE 0, LIMIT $N, LENGTH $L, SRCETH $macA, DSTETH $macB, SRCIP 10.0.0.100, DSTIP 10.0.0.101, FLOWS 1, FLOWSIZE $N)
-> MarkMACHeader
//EnsureDPDKBuffer will copy the packet inside a DPDK buffer, so there is no more copies (not even to the NIC) afterwards when we replay the packet many time
-> EnsureDPDKBuffer
//ReplayUqueue pulls all packets from its input, and replay them from memory $S amount of time
-> replay :: ReplayUnqueue(STOP $S, ACTIVE false, QUICK_CLONE 0)
-> ic0 :: AverageCounter()
-> ToDPDKDevice($portA, BLOCKING $blocking, VERBOSE $verbose);

//It is good practice to pin any source to let FastClick know what will eat the CPU and allocate FromDPDKDevice threads accordingly. It also help you know what you're doing. Multithreading is everything but magic.
StaticThreadSched(replay 0)

//Send packets from time to time on port 1 with our virtual DST MAC as SRC MAC to let the switch learn about us and send us the traffic when pktgen starts
FastUDPFlows(RATE 1, LIMIT 1, LENGTH $L, SRCETH $macB, DSTETH $macA, SRCIP 10.0.0.101, DSTIP 10.0.0.100, FLOWS 1, FLOWSIZE 1)
-> Unqueue
-> ToDPDKDevice($portB, BLOCKING $blocking, VERBOSE $verbose);

//###################
// RX
//###################
fd0 :: FromDPDKDevice($portB, PROMISC true, VERBOSE $verbose) -> oc0 :: AverageCounter() -> Discard;
fd1 :: FromDPDKDevice($portA, PROMISC true, VERBOSE $verbose) -> Discard;

DriverManager(	wait 100ms,  //Let the time for our MAC discovering packets flow
				write replay.active true, //Launch replay
				wait, wait 10ms, //Wait for Replay to stop, then 10ms for the the last packets to comes
				print "Number of packets sent : $(ic0.count)",
				print "Number of packets received : $(oc0.count)", 
				print "TX rate : $(ic0.link_rate)", 
				print "RX rate : $(oc0.link_rate)",
				);
