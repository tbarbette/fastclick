/*
 * Multi-Threaded packet generator with memory preload. Uses DPDK.
 *
 * This version does not compute latency or receive packets back on another interface.
 * It uses 4 threads to replay packets, after they're preloaded in memory.
 *
 * Example usage: bin/click --dpdk -l 0-15 -- conf/pktgen/pktgen-l2-mt-replaypcap.click trace=/tmp/trace.pcap
 */

define($trace /path/to/trace.pcap)
define($limit 500000) //Number of packets to preload
define($replay_count 10) //Number of time we'll replay those packets

d :: DPDKInfo(NB_SOCKET_MBUF  2040960) //Should be a bit more than 4 times the limit

define($INsrcmac 04:3f:72:dc:4a:64)
define($INdstmac 50:6b:4b:f3:7c:70)

define($bout 32)
define($ignore 0)

define($txport 0000:51:00.0)
define($quick true)
define($txverbose 99)


fdIN :: FromDump($trace, STOP false, TIMING false)

tdIN :: ToDPDKDevice($txport, BLOCKING true, BURST $bout, VERBOSE $txverbose, IQUEUE $bout, NDESC 0, TCO 1)


elementclass NoNumberise { $magic |
    input
    -> Strip(14) 
    -> check :: CheckIPHeader(CHECKSUM false) 
    -> Unstrip(14) 
    -> output
}

fdIN
    -> rr :: PathSpinlock;

elementclass Generator { $magic |
    input
      -> EnsureDPDKBuffer
      -> rwIN :: EtherRewrite($INsrcmac,$INdstmac)
      -> Pad()
      -> NoNumberise($magic)
      -> replay :: ReplayUnqueue(STOP 0, STOP_TIME 0, QUICK_CLONE $quick, VERBOSE false, ACTIVE true, LIMIT 500000, TIMING 0)

      -> avgSIN :: AverageCounter(IGNORE $ignore)
      -> { input[0] -> MarkIPHeader(OFFSET 14) -> ipc :: IPClassifier(tcp or udp, -) ->  ResetIPChecksum(L4 true) -> [0]output; ipc[1] -> [0]output; }
      -> output;
}

rr -> gen0 :: Generator(\<5601>) -> tdIN;StaticThreadSched(gen0/replay 0);
rr -> gen1 :: Generator(\<5602>) -> tdIN;StaticThreadSched(gen1/replay 1);
rr -> gen2 :: Generator(\<5603>) -> tdIN;StaticThreadSched(gen2/replay 2);
rr -> gen3 :: Generator(\<5604>) -> tdIN;StaticThreadSched(gen3/replay 3);

run_test :: Script(TYPE PASSIVE,
            wait 0s,
            print "EVENT GEN_BEGIN",
            print "Starting bandwidth computation !",
            print "$GEN_PRINT_START",
            label end);


//To display stats every seconds, change PASSIVE by ACTIVE
display_th :: Script(TYPE PASSIVE,
                    print "Starting iterative...",

                     set stime $(now),
                     label g,
	    	         write gen0/avgSIN.reset, write RIN/avg0.reset, write gen1/avgSIN.reset, write RIN/avg1.reset, write gen2/avgSIN.reset, write RIN/avg2.reset, write gen3/avgSIN.reset, write RIN/avg3.reset,
                     wait 1,
                     set diff $(sub $(now) $time),
                     print "Diff $diff",
                     set time $(sub $(now) $stime),
                     set sent $(avgSIN.add count),
        		     print "IGEN-$time-RESULT-ICOUNT $received",
                     print "IGEN-$time-RESULT-IDROPPED $(sub $sent $received)",
                     print "IGEN-$time-RESULT-IDROPPEDPS $(div $(sub $sent $received) $diff)",
                     print "IGEN-$time-RESULT-ITHROUGHPUT $rx",

                     print "IGEN-$time-RESULT-ITX $tx",
                     print "IGEN-$time-RESULT-ILOSS $(sub $rx $tx)",
                     goto g);



avgSIN :: HandlerAggregate( ELEMENT gen0/avgSIN,ELEMENT gen1/avgSIN,ELEMENT gen2/avgSIN,ELEMENT gen3/avgSIN );


link_initialized :: Script(TYPE PASSIVE,
    print "Link initialized !",
    wait 1s,
    print "Starting replay...",
    write gen0/avgSIN.reset, write RIN/avg0.reset, write gen1/avgSIN.reset, write RIN/avg1.reset, write gen2/avgSIN.reset, write RIN/avg2.reset, write gen3/avgSIN.reset, write RIN/avg3.reset,
    write gen0/replay.stop $replay_count, write gen0/replay.active true, write gen1/replay.stop $replay_count, write gen1/replay.active true, write gen2/replay.stop $replay_count, write gen2/replay.active true, write gen3/replay.stop $replay_count, write gen3/replay.active true,
    write run_test.run 1,
    print "Time is $(now)",
);

DriverManager(
                print "Waiting for preload...",
                pause, pause, pause, pause,
                wait 2s,
                write link_initialized.run,
                label waitagain,
                set starttime $(now),
                pause,
                set stoptime $(now),
                set sent $(avgSIN.add count),
                print "RESULT-TESTTIME $(sub $stoptime $starttime)",
                print "RESULT-SENT $sent",
                print "RESULT-TX $(avgSIN.add link_rate)",
                print "RESULT-TXPPS $(avgSIN.add rate)",
                label end,
                print "EVENT GEN_DONE",
                stop);
