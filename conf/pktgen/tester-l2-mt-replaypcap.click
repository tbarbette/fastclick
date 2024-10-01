/*
 * Multi-Threaded packet generator with memory preload. Uses DPDK.
 *
 * It uses 4 threads to replay packets, after they're preloaded in memory.
 *
 * Example usage: bin/click --dpdk -l 0-15 -- conf/pktgen/tester-l2-mt-replaypcap.click trace=/tmp/trace.pcap
 */

define($trace /path/to/trace.pcap)

d :: DPDKInfo(NB_SOCKET_MBUF  2040960)

define($bout 32)
define($INsrcmac 04:3f:72:dc:4a:64)
define($RAW_INsrcmac 043f72dc4a64)

define($INdstmac 50:6b:4b:f3:7c:70)
define($RAW_INdstmac 506b4bf37c70)

define($ignore 0)
define($replay_count 10)

define($txport 0000:51:00.0)
define($rxport 0000:51:00.0)
define($quick true)
define($txverbose 99)
define($rxverbose 99)
define($limit 500000)

fdIN :: FromDump($trace, STOP false, TIMING false)

tdIN :: ToDPDKDevice($txport, BLOCKING true, BURST $bout, VERBOSE $txverbose, IQUEUE $bout, NDESC 0, TCO 1)

elementclass Numberise { $magic |
    input-> Strip(14) -> check :: CheckIPHeader(CHECKSUM false) -> nPacket :: NumberPacket(42) -> StoreData(40, $magic) -> SetIPChecksum -> Unstrip(14) -> output
}

//Packets read from the trace are sent to PathSpinLock, that will be queried by multiple thread (and protected).
fdIN
	-> rr :: PathSpinlock;

elementclass Generator { $magic |
input
  -> EnsureDPDKBuffer
  -> rwIN :: EtherRewrite($INsrcmac,$INdstmac)
  -> Pad()
   -> Numberise($magic)
  -> replay :: ReplayUnqueue(STOP 0, STOP_TIME 0, QUICK_CLONE $quick, VERBOSE false, ACTIVE true, LIMIT 500000, TIMING 0)
  -> rt :: RecordTimestamp(N $limit, OFFSET 56)
  -> avgSIN :: AverageCounter(IGNORE $ignore)
     -> { input[0] -> MarkIPHeader(OFFSET 14) -> ipc :: IPClassifier(tcp or udp, -) ->  ResetIPChecksum(L4 true) -> [0]output; ipc[1] -> [0]output; }
  -> output;
}

rr -> gen0 :: Generator(\<5601>) -> tdIN;StaticThreadSched(gen0/replay 0);
rr -> gen1 :: Generator(\<5602>) -> tdIN;StaticThreadSched(gen1/replay 1);
rr -> gen2 :: Generator(\<5603>) -> tdIN;StaticThreadSched(gen2/replay 2);
rr -> gen3 :: Generator(\<5604>) -> tdIN;StaticThreadSched(gen3/replay 3);

receiveIN :: FromDPDKDevice($rxport, VERBOSE $rxverbose, MAC $INsrcmac, PROMISC false, PAUSE none, NDESC 0, MAXTHREADS 4, NUMA false, ACTIVE 1)

elementclass Receiver { $mac, $dir |
    input[0]
 -> c :: Classifier(0/$mac,-)
    -> Strip(14)
    -> CheckIPHeader(CHECKSUM false)
   -> magic :: Classifier( 40/5601, 40/5602, 40/5603, 40/5604, -) 

    c[1] //Not for this computer or broadcasts
    -> Discard;

magic[0] -> tsd0 ::  TimestampDiff(gen0/rt, OFFSET 42, N  1000000 , SAMPLE 10 ) -> Unstrip(14) ->  avg0 :: AverageCounterIMP(IGNORE $ignore) -> Discard;  tsd0[1] -> Print('WARNING: Untimestamped packet on thread 0', 64) -> Discard;
magic[1] -> tsd1 ::  TimestampDiff(gen1/rt, OFFSET 42, N  1000000 , SAMPLE 10 ) -> Unstrip(14) ->  avg1 :: AverageCounterIMP(IGNORE $ignore) -> Discard;  tsd1[1] -> Print('WARNING: Untimestamped packet on thread 1', 64) -> Discard;
magic[2] -> tsd2 ::  TimestampDiff(gen2/rt, OFFSET 42, N  1000000 , SAMPLE 10 ) -> Unstrip(14) ->  avg2 :: AverageCounterIMP(IGNORE $ignore) -> Discard;  tsd2[1] -> Print('WARNING: Untimestamped packet on thread 2', 64) -> Discard;
magic[3] -> tsd3 ::  TimestampDiff(gen3/rt, OFFSET 42, N  1000000 , SAMPLE 10 ) -> Unstrip(14) ->  avg3 :: AverageCounterIMP(IGNORE $ignore) -> Discard;  tsd3[1] -> Print('WARNING: Untimestamped packet on thread 3', 64) -> Discard;


avg :: HandlerAggregate( ELEMENT avg0,ELEMENT avg1,ELEMENT avg2,ELEMENT avg3 );

    magic[4]
    -> Unstrip(14)
    -> Print("WARNING: Unknown magic / untimestamped packet", -1)
    -> Discard;
}

receiveIN -> RINswitch :: Switch(2)[0] -> RIN :: Receiver($RAW_INsrcmac,"IN");


//----------------
//Link initializer
//----------------
adv0 :: FastUDPFlows(RATE 0, LIMIT -1, LENGTH 64, SRCETH $INsrcmac, DSTETH $INsrcmac, SRCIP 10.100.0.1, DSTIP 10.100.0.1, FLOWS 1, FLOWSIZE 1)
    -> advq0 :: RatedUnqueue(1)
    -> tdIN;

//Check that it received its packet from 2 outputs and emits packets on output 0 when it's the case
linkoklock :: PathSpinlock() [0]
  -> linkok :: Script(TYPE PACKET,
            write advq0.active false,
            write adv0.active false,
            return 0
            )


RINswitch[2]
    -> Classifier(0/$RAW_INsrcmac)
    -> Print -> [0]linkoklock


//-----------------

linkok ->
link_initialized :: Script(TYPE PACKET,
    print "Link initialized !",
    write RINswitch.switch -1,
    print "IN has $(RIN/nPacket.count) packets",
    wait 1s,

    print "Starting replay...",
    write gen0/avgSIN.reset, write RIN/avg0.reset, write gen1/avgSIN.reset, write RIN/avg1.reset, write gen2/avgSIN.reset, write RIN/avg2.reset, write gen3/avgSIN.reset, write RIN/avg3.reset,
    write RINswitch.switch 0 ,
    write gen0/replay.stop $replay_count, write gen0/replay.active true, write gen1/replay.stop $replay_count, write gen1/replay.active true, write gen2/replay.stop $replay_count, write gen2/replay.active true, write gen3/replay.stop $replay_count, write gen3/replay.active true,

    write run_test.run 1,
    print "Time is $(now)",
);

run_test :: Script(TYPE PASSIVE,
            wait 0s,
            print "EVENT GEN_BEGIN",
            print "Starting bandwidth computation !",
            print "$GEN_PRINT_START",
            goto end $(eq 0 0),
            write display_th.run 1,
            label end);
display_th :: Script(TYPE PASSIVE,
                    print "Starting iterative...",
                     set indexA 0,
                     set indexB 0,
                     set indexC 0,
                     set indexD 0,
                     set stime $(now),
                     label g,
		     write gen0/avgSIN.reset, write RIN/avg0.reset, write gen1/avgSIN.reset, write RIN/avg1.reset, write gen2/avgSIN.reset, write RIN/avg2.reset, write gen3/avgSIN.reset, write RIN/avg3.reset,
                     wait 1,
                     set diff $(sub $(now) $time),
                     print "Diff $diff",
                     set time $(sub $(now) $stime),
                     set sent $(avgSIN.add count),
                     set received $(RIN/avg.add count),
                     set bytes $(RIN/avg.add byte_count),
		     set rx $(RIN/avg.add link_rate),
		     print "IGEN-$time-RESULT-ICOUNT $received",
                     print "IGEN-$time-RESULT-IDROPPED $(sub $sent $received)",
                     print "IGEN-$time-RESULT-IDROPPEDPS $(div $(sub $sent $received) $diff)",
                     print "IGEN-$time-RESULT-ITHROUGHPUT $rx",

                     //If no packets, do not print latency
                     goto g $(eq $(RIN/tsdA.index) $indexA),
                     print "",
                     /*print "IGEN-$time-RESULT-ILATENCY $(div $(add $(RIN/tsdA.average $indexA) $(RIN/tsdB.average $indexB) $(RIN/tsdC.average $indexC) $(RIN/tsdD.average $indexD)) 4)",
                     print "IGEN-$time-RESULT-ILAT99 $(div $(add $(RIN/tsdA.perc99 $indexA) $(RIN/tsdB.perc99 $indexB) $(RIN/tsdC.perc99 $indexC) $(RIN/tsdD.perc99 $indexD)) 4)",

                     print "IGEN-$time-RESULT-ILAT95 $(div $(add $(RIN/tsdA.perc95 $indexA) $(RIN/tsdB.perc95 $indexB) $(RIN/tsdC.perc95 $indexC) $(RIN/tsdD.perc95 $indexD)) 4)",

                     print "IGEN-$time-RESULT-ILAT05 $(div $(add $(RIN/tsdA.perc 5 $indexA) $(RIN/tsdB.perc 5 $indexB) $(RIN/tsdC.perc 5 $indexC) $(RIN/tsdD.perc 5 $indexD)) 4)",

                     print "IGEN-$time-RESULT-ITX $tx",
                     print "IGEN-$time-RESULT-ILOSS $(sub $rx $tx)",

                     set indexA $(RIN/tsdA.index),
                     set indexB $(RIN/tsdB.index),
                     set indexC $(RIN/tsdC.index),
                     set indexD $(RIN/tsdD.index),
                    */
                     goto g)




RINswitch[1]->Print(LATEIN) -> Discard;

tsd :: HandlerAggregate( ELEMENT RIN/tsd0,ELEMENT RIN/tsd1,ELEMENT RIN/tsd2,ELEMENT RIN/tsd3 );

avgSIN :: HandlerAggregate( ELEMENT gen0/avgSIN,ELEMENT gen1/avgSIN,ELEMENT gen2/avgSIN,ELEMENT gen3/avgSIN );


DriverManager(
                print "Waiting for preload...",
                pause, pause, pause, pause,
                goto waitagain $(eq 1 0),
                wait 2s,
                write advq0.active false,
                write adv0.active false,
                write link_initialized.run,
                label waitagain,
                set starttime $(now),
                pause,
//                pause, pause, pause, pause,
                set stoptime $(now),
                read receiveIN.hw_count,
                read receiveIN.count,
                set sent $(avgSIN.add count),
                set count $(RIN/avg.add count),
                set bytes $(RIN/avg.add byte_count),
                set rx $(RIN/avg.add link_rate),

                print "RESULT-TESTTIME $(sub $stoptime $starttime)",
                print "RESULT-RCVTIME $(RIN/avg0.time)",
                goto adump $(eq 0 0),
/*                print "Dumping latency samples to $LATENCYDUMP",
                print >$LATENCYDUMP $(RIN/tsdA.dump_list),
                print >>$LATENCYDUMP $(RIN/tsdB.dump_list),
                print >>$LATENCYDUMP $(RIN/tsdC.dump_list),
                print >>$LATENCYDUMP $(RIN/tsdD.dump_list),
*/
                label adump,
                goto ldump $(eq 1 0),
                print "RESULT-LATENCY $(tsd.avg average)",
                print "RESULT-LAT00 $(tsd.avg min)",
                print "RESULT-LAT01 $(tsd.avg perc01)",
                print "RESULT-LAT50 $(tsd.avg median)",
                print "RESULT-LAT95 $(tsd.avg perc95)",
                print "RESULT-LAT99 $(tsd.avg perc99)",
                print "RESULT-LAT100 $(tsd.avg max)",
                label ldump,
                           goto end $(eq 0 1),

                print "RESULT-THROUGHPUT $rx",
                print "RESULT-COUNT $count",
                print "RESULT-BYTES $bytes",
                print "RESULT-SENT $sent",
                print "RESULT-DROPPED $(sub $sent $count)",
                print "RESULT-TX $(avgSIN.add link_rate)",
                print "RESULT-TXPPS $(avgSIN.add rate)",
                print "RESULT-PPS $(RIN/avg.add rate)",
                label end,
                print "EVENT GEN_DONE",
                read receiveIN.xstats,
                stop);
