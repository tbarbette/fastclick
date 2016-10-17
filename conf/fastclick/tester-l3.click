/*
 * This is a L3 version of tester. Port 0 is expected to be attached to the
 * same switch than the WAN port of the DUT, and port 1 to LAN. You could 
 * attach port0 to the WAN port of the DUT directly, but you would loose the
 * WAN connection as this configuration does not include a NAT or router, 
 * or DHCP.
 *
 * This configuration will send one UDP flow from LAN to WAN at max rate and
 * will print statistics about the throughput and loss of the DUT.
 */

define($block true)
define($wanmac 00:18:0A:12:14:28)
define($lanmac 00:18:0A:12:14:29)
define($wanip 192.168.128.50)
define($lanip 192.168.129.50)

replay0 :: MultiReplayUnqueue(STOP $S, QUICK_CLONE 0)
replay1 :: MultiReplayUnqueue(STOP $S, QUICK_CLONE 0)

DPDKInfo(65536)

is0 :: FastUDPFlows(RATE 0, LIMIT $N, LENGTH $L, SRCETH 90:e2:ba:c3:77:d2, DSTETH $wanmac, SRCIP 10.0.0.100, DSTIP 192.168.0.100, FLOWS 1, FLOWSIZE $N)
-> MarkMACHeader
-> EnsureDPDKBuffer
-> replay0
-> ic0 :: AverageCounter()
-> ToDPDKDevice(0, BLOCKING $block)

fd0 :: FromDPDKDevice(0) -> oc0 :: AverageCounter() -> Discard
fd1 :: FromDPDKDevice(1) -> oc1 :: AverageCounter() -> Discard

StaticThreadSched(is0 0, is1 1)
StaticThreadSched(replay0 0, replay1 1)
StaticThreadSched(fd0 2, fd1 3)


DriverManager(wait, wait, wait 10ms, print $(ic0.count), print $(oc0.count), print $(ic1.count), print $(oc1.count),
							 print $(ic0.link_rate), print $(oc0.link_rate), print $(ic1.link_rate), print $(oc1.link_rate))
