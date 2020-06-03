/**
 * A NIC offloading example using DPDK's Flow API.
 * Three NIC rules are offloaded, each dispatching matched packets
 * to one of the first 3 NIC queues. Another 2 queues are used by RSS.
 */

/**
 * Deploy as follows:
 * sudo ../../bin/click --dpdk -l 0-4 -w 0000:11:00.0 -v -- dpdk-flow-parser.click
 */

define(
	$ifacePCI0  0000:11:00.0,
	$queues     5,
	$threads    $queues,
	$numa       false,
	$mode       flow,    // Rx mode required by the DPDK Flow Rule Manager
	$verbose    99,
	$rules      test_dpdk_nic_rules        // Better provide absolute path
);

// NIC in Flow Rule Manager's mode
fd0 :: FromDPDKDevice(
	PORT $ifacePCI0, MODE $mode,
	N_QUEUES $queues, NUMA $numa,
	THREADOFFSET 0, MAXTHREADS $threads,
	FLOW_RULES_FILE $rules,
	VERBOSE $verbose, PAUSE full
);

fd0
	-> classifier :: Classifier(12/0800, -)
	-> Strip(14)
	-> check :: CheckIPHeader(VERBOSE true)
	-> IPPrint(ETHER true, LENGTH true)
	-> Unstrip(14)
	-> legit :: AverageCounterMP()
	-> Discard;

dropped :: AverageCounterMP();

classifier[1] -> dropped;
check[1] -> dropped;
dropped	-> Discard;

DriverManager(
	pause,
	print "",
	print "[Rule 1 - Queue 0]: "$(fd0.xstats rx_q0packets)" packets - "$(fd0.xstats rx_q0bytes)" bytes",
	print "[Rule 2 - Queue 1]: "$(fd0.xstats rx_q1packets)" packets - "$(fd0.xstats rx_q1bytes)" bytes",
	print "[Rule 3 - Queue 2]: "$(fd0.xstats rx_q2packets)" packets - "$(fd0.xstats rx_q2bytes)" bytes",
	print "[   RSS - Queue 3]: "$(fd0.xstats rx_q3packets)" packets - "$(fd0.xstats rx_q3bytes)" bytes",
	print "[   RSS - Queue 4]: "$(fd0.xstats rx_q4packets)" packets - "$(fd0.xstats rx_q4bytes)" bytes",
	print "",
	print "   IPv4: "$(legit.count),
	print "Dropped: "$(dropped.count),
	stop
);
