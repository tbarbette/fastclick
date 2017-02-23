/* 
 * This file implements a fast L2 UDP packet generator
 *
 * This can be used to test the throughput of a DUT, using a receiver-l2.click
 * on some other end of a switch
 *
 * A launch line would be :
 *   sudo bin/click -c 0x1 -n 4 -- conf/fastclick/pktgen-l2.click L=60 S=1000000 N=100
 */

//You do not need to change these to the real ones, just have the dmac match the receiver's one
define($mymac 90:e2:ba:c3:79:66)
define($dmac 90:e2:ba:c3:76:6e)
//Ip are just for a convenient payload as this is l2
define($myip 192.168.130.13)
define($dstip 192.168.128.13)

//Explained in loop.click
define($verbose 3)
define($blocking true)

//###################
// TX
//###################
//Create a UDP flow of $N packets
FastUDPFlows(RATE 0, LIMIT $N, LENGTH $L, SRCETH $mymac, DSTETH $dmac, SRCIP $myip, DSTIP $dstip, FLOWS 1, FLOWSIZE $N)
-> MarkMACHeader
//EnsureDPDKBuffer will copy the packet inside a DPDK buffer, so there is no more copies (not even to the NIC) afterwards when we replay the packet many time
-> EnsureDPDKBuffer
//MutliReplayUqueue pulls all packets from its input, and replay them from memory $S amount of time
-> replay :: MultiReplayUnqueue(STOP -1, ACTIVE false, QUICK_CLONE 1)
-> ic0 :: AverageCounter()
-> td :: ToDPDKDevice(0, BLOCKING $blocking, VERBOSE $verbose)

//It is good practice to pin any source to let FastClick know what will eat the CPU and allocate FromDPDKDevice threads accordingly. It also help you know what you're doing. Multithreading is everything but magic.
StaticThreadSched(replay 0)

//###################
// RX
//###################
fd :: FromDPDKDevice(0, PROMISC true, VERBOSE $verbose)
-> Discard

Script(TYPE ACTIVE,
 				wait 5ms,
				write replay.active true, //Launch replay
				label start,
				print "Number of packets sent : $(ic0.count)",
				print "TX rate : $(ic0.link_rate)", 
				wait 1s,
				goto start
				)
