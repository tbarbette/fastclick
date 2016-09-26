/* 
 * This configuration count the number of packets received on one interface, it
 * does not respond to ARP packets and is therefore not l3 but l2.
 *
 * As this configuration does not send the packets it receive, it runs best if
 *  compiled with --disable-dpdk-pool, as the internal packet pool would be 
 *  filled up by DPDK packets, the input only using Click packet to associate
 *  them with DPDK buffer.
 * 
 * This configuration launch a packet every 1s to the link so a potential l2
 * switch can learn our mac address.
 *
 * A launch line would be :
 *   sudo bin/click -c 0xf -n 4 -- conf/fastclick/receiver-l2.click
 */

//You do not need to change these, we send a packet with our virtual mac source before launching the pktgen so any switch can learn about us
define($mymac 90:e2:ba:c3:76:6e)
define($myip 192.168.128.13)

//Explained in loop.click
define($verbose 3)
define($blocking true)

//###################
// TX
//###################
//Advertise our mac address every 1sec so the switch can learn about us
FastUDPFlows(RATE 0, LIMIT -1, LENGTH 60, SRCETH $mymac, DSTETH ff:ff:ff:ff:ff:ff, SRCIP $myip, DSTIP 255.255.255.255, FLOWS 1, FLOWSIZE 1)
-> TimedUnqueue(1,1)
-> td :: ToDPDKDevice(0, BLOCKING $blocking, VERBOSE $verbose)

//###################
// RX
//###################
fd0 :: FromDPDKDevice(0, PROMISC true, VERBOSE $verbose)
-> oc0 :: AverageCounter() -> Discard

Script(TYPE ACTIVE,
				label start,
				wait 1s,
				print "Number of packets received : $(oc0.count)", 
				print "RX rate : $(oc0.link_rate)",
				write oc0.reset,
				goto start
				)
