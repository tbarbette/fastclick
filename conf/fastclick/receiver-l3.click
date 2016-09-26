/* 
 * This configuration count the number of packets received on one interface, it
 * does respond to ARP packets and is therefore L3
 *
 * As this configuration does not send the packets it receive, it runs best if
 *  compiled with --disable-dpdk-pool, as the internal packet pool would be 
 *  filled up by DPDK packets, the input only using Click packet to associate
 *  them with DPDK buffer.
 *
 * A launch line would be :
 *   sudo bin/click -c 0xf -n 4 -- conf/fastclick/receiver-l3.click
 */

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
