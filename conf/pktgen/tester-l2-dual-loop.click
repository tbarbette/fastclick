/**
 * Repeat tester-l2-dual for every 4 byte packet size up to 1500.
 * Read first tester-l2-dual for comprehension.
 */

define($block true)

replay0 :: ReplayUnqueue(STOP $S, QUICK_CLONE 0, ACTIVE false)
replay1 :: ReplayUnqueue(STOP $S, QUICK_CLONE 0, ACTIVE false)

DPDKInfo(65536)

is0 :: FastUDPFlows(RATE 0, LIMIT $N, LENGTH $L, SRCETH 90:e2:ba:c3:77:d2, DSTETH 90:e2:ba:c3:77:70, SRCIP 10.0.0.100, DSTIP 10.0.0.101, FLOWS 1, FLOWSIZE $N)
-> MarkMACHeader
//-> EtherRewrite(90:e2:ba:c3:77:70, 90:e2:ba:c3:77:d2)
-> EnsureDPDKBuffer
-> [0]replay0[0]
-> ic0 :: AverageCounter()
-> ToDPDKDevice(0, BLOCKING $block)

is1 :: FastUDPFlows(RATE 0, LIMIT $N, LENGTH $L, SRCETH 90:e2:ba:c3:77:70, DSTETH 90:e2:ba:c3:77:d2, SRCIP 10.0.0.101, DSTIP 10.0.0.100, FLOWS 1, FLOWSIZE $N)
-> MarkMACHeader
//-> EtherRewrite(90:e2:ba:c3:77:d2, 90:e2:ba:c3:77:70)
-> EnsureDPDKBuffer
-> [0]replay1[0]
-> ic1 :: AverageCounter()
-> ToDPDKDevice(1, BLOCKING $block)

fd0 :: FromDPDKDevice(0) -> oc0 :: AverageCounter() -> Discard
fd1 :: FromDPDKDevice(1) -> oc1 :: AverageCounter() -> Discard

StaticThreadSched(is0 0, is1 1)
StaticThreadSched(replay0 0, replay1 1)
StaticThreadSched(fd0 2, fd1 3)

make_test :: Script(TYPE PASSIVE,
)

DriverManager(	set LENGTH $L,
				label start,
				print "Launching test with L=$LENGTH",
				write oc0.reset,
				write oc1.reset,
				write ic0.reset,
				write ic1.reset,
				write is0.length $LENGTH,
				write is1.length $LENGTH,
				write is0.reset,
				write is1.reset,
				set REP $(mul $(idiv $S $LENGTH) 60),
				write replay0.stop $REP,
				write replay1.stop $REP,
				write replay0.active true,
				write replay1.active true,
				write replay0.reset,
				write replay1.reset,
				wait, wait,
				wait 10ms, print $(ic0.count), print $(oc0.count), print $(ic1.count), print $(oc1.count),
				print $(ic0.link_rate), print $(oc0.link_rate), print $(ic1.link_rate), print $(oc1.link_rate),
				set orate $(add  $(oc0.link_rate) $(oc1.link_rate)),
				set inbits $(add $(ic0.link_count) $(ic1.link_count)),
				set outbits $(add $(oc0.link_count) $(oc1.link_count)),
				set oloss $(div $(sub $inbits $outbits) $(inbits)),
				print "RESULT $LENGTH $orate $oloss",
				set LENGTH $(add $LENGTH 4),
				goto start $(le $LENGTH 1500),
				print "All test finished !",
				)
