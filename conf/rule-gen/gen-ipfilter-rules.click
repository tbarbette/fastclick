/**
 * Generate Click IPFilter/IPClassifier rules out of a trace.
 * Before dumping the rules, the script changes the packets' Ethernet addresses
 * according to the respective variables in order to fit your needs.
 */

// sudo bin/click --dpdk -l 0-0 --socket-mem 62,0 --huge-dir /mnt/hugepages -v -- conf/rule-gen/gen-ipfilter-rules.click pattern=IPFILTER outFile=rules_ipfilter
// sudo bin/click --dpdk -l 0-0 --socket-mem 62,0 --huge-dir /mnt/hugepages -v -- conf/rule-gen/gen-ipfilter-rules.click pattern=IPCLASSIFIER outFile=rules_ipclassifier

////////////////////////////////////////////////////////////
// Testbed configuration
////////////////////////////////////////////////////////////
define(
	$N           20000000,  // Total number of packets
	$C           10000,     // Number of clients
	$F           1,         // Number of flows per client
	$PF          2000,      // Packets per flow
	$PS          64,        // Packet size
	$PROTO       UDP,
	$traceDir    /mnt/traces/synthetic, // Traces' directory
	$trace       $traceDir"/trace-$PROTO-N${N}-C${C}-F${F}-PF${PF}-PS${PS}.pcap",
	$inSrcMac    b8:83:03:6f:43:40,
	$inDstMac    b8:83:03:6f:43:38,
	$ignore      0,

	$rulesNb     1000,
	$pattern     IPFILTER,
	$withSrcIp   true,
	$withDstIp   true,
	$withSrcPort true,
	$withDstPort true,
	$outFile     rules_ipfilter,

	$activePrint false,
);

d :: DPDKInfo(NB_SOCKET_MBUF 16777215, NB_SOCKET_MBUF 8191);

fdIN :: FromDump($trace, STOP true, TIMING false);

ruleGen :: GenerateIPFilterRules(
	NB_RULES $rulesNb,
	PATTERN_TYPE $pattern,
	KEEP_SADDR $withSrcIp, KEEP_DADDR $withDstIp,
	KEEP_SPORT $withSrcPort, KEEP_DPORT $withDstPort,
	OUT_FILE $outFile
);

fdIN
	-> rwIN :: EtherRewrite($inSrcMac, $inDstMac)
	-> avgIN :: AverageCounterMP(IGNORE $ignore)
	-> Strip(14)
	-> CheckIPHeader()
	-> IPPrint(ETHER true, LENGTH true, ACTIVE $activePrint)
	-> ruleGen
	-> Discard();

DriverManager(
	pause,
	print "",
	print "Trace: "$trace,
	print "",
	print "# of packets in trace: "$(avgIN.count),
	print "# of   flows in trace: "$(ruleGen.flows_nb),
	print "",
	print "Dumping rules to file: "$outFile,
	print $(ruleGen.dump_to_file),
	//print $(ruleGen.dump),
	print "",
	print "Rules for trace: "$(ruleGen.rules_nb),

	stop
);
