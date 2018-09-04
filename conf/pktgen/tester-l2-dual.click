/*
 * This is a dual flow version of tester-l2.click, sending UDP flows at max rate
 *  to two DPDK ports and expect to receive the packets back on the opposite port to display statistics.
 *
 * Please read tester-l2.click before for full comprehension !
 */
define($block true)

replay0 :: ReplayUnqueue(STOP $S, QUICK_CLONE 0)
replay1 :: ReplayUnqueue(STOP $S, QUICK_CLONE 0)

DPDKInfo(65536)

is0 :: FastUDPFlows(RATE 0, LIMIT $N, LENGTH $L, SRCETH 90:e2:ba:c3:77:d2, DSTETH 90:e2:ba:c3:77:70, SRCIP 10.0.0.100, DSTIP 10.0.0.101, FLOWS 1, FLOWSIZE $N)
-> MarkMACHeader
-> EnsureDPDKBuffer
-> replay0
-> ic0 :: AverageCounter()
-> ToDPDKDevice(0, BLOCKING $block);
//-> Discard;

is1 :: FastUDPFlows(RATE 0, LIMIT $N, LENGTH $L, SRCETH 90:e2:ba:c3:77:70, DSTETH 90:e2:ba:c3:77:d2, SRCIP 10.0.0.101, DSTIP 10.0.0.100, FLOWS 1, FLOWSIZE $N)
-> MarkMACHeader
-> EnsureDPDKBuffer
-> replay1
-> ic1 :: AverageCounter()
-> ToDPDKDevice(1, BLOCKING $block);
//-> Discard;


fd0 :: FromDPDKDevice(0) -> oc0 :: AverageCounter() -> Discard;
fd1 :: FromDPDKDevice(1) -> oc1 :: AverageCounter() -> Discard;

StaticThreadSched(is0 0, is1 1);
StaticThreadSched(replay0 0, replay1 1);
StaticThreadSched(fd0 2, fd1 3);


DriverManager(wait, wait, wait 10ms, print $(ic0.count), print $(oc0.count), print $(ic1.count), print $(oc1.count),
							 print $(ic0.link_rate), print $(oc0.link_rate), print $(ic1.link_rate), print $(oc1.link_rate))
