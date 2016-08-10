/* 
 *This file implements a fast L2 pktgen which sends UDP traffic on one NIC towards a second NIC
 *
 * This can be used to test the throughput of a L2 switch.
 *
 * The second port is used to receive the traffic back and compute 
 * some statistics. We do not respond to ARP packets, hence the L2 limitation.
 *
 * A launch line would be :
 *   sudo bin/click -c 0x7 -n 4 -- conf/fastclick/pktgen.click L=60 S=1000000 N=100
 */

//!!!!
//Please read loop.click first to learn about some FastClick basics!
//!!!!

//You do not need to change these, we send a packet with our virtual mac source before launching the pktgen so any switch can learn about us
define($smac 90:e2:ba:c3:76:6e)
define($srcip 192.168.128.13)

//Explained in loop.click
define($verbose 3)
define($blocking true)

//###################
// TX
//###################
td :: ToDPDKDevice(0, BLOCKING $blocking, VERBOSE $verbose)

//###################
// RX
//###################
fd0 :: FromDPDKDevice(0, PROMISC true, VERBOSE $verbose)
-> arp_c :: Classifier(12/0800, 12/0806 20/0001, 12/0806 20/0002, -)

arp_c[0] -> oc0 :: AverageCounter() -> Discard
arp_c[1] -> Print("ARP QUERY") -> arp_r :: ARPResponder($srcip $smac) -> Print("OUR RESP") ->  td
arp_c[2] -> Print("ARP RESP") -> Discard
arp_c[3] -> Print("OTHER") -> Discard



Script(	TYPE ACTIVE,
				label start,
				wait 1s,
				print "Number of packets received : $(oc0.count)", 
				print "RX link rate : $(oc0.link_rate)",
				write oc0.reset,
				goto start
				)
