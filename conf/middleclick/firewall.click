define ($IN 0000:17:00.1)
define ($INETH enp23s0f1)
define ($INsrcmac 50:6b:4b:43:88:cb)
define ($INsrcip 10.2.0.60)
define ($INdstip 10.2.0.40)
define ($INGW 139.165.0.2)
define ($GWINmac 98:03:9b:2c:0e:b4)
define ($INTERNALNET 0.0.0.0/0)

//#############################

define($aggcache true)
define($builder 1)
fdIN :: { f :: FromDPDKDevice($IN, VERBOSE 99, RSS_AGGREGATE $aggcache, N_QUEUES -1, PROMISC true) -> MarkMACHeader -> [0] }
tdIN :: FlowUnstrip(14) -> EtherRewrite($INsrcmac, $GWINmac)
-> tIN :: ToDPDKDevice($IN, VERBOSE 99, BLOCKING false)

/**
 * Output 0 : IP
 * Output 1 : Passthrough firewall
 * Output 2 : Unknow traffic
 */
elementclass ARPDispatcher {
	input[0]->CTXDispatcher(
			12/0800,
			12/0806,
			-)[0,1,2]=>[0,1,2]output
}

dispIN :: ARPDispatcher
elementclass FwIN { input ->
                CTXDispatcher(
                                //12/8ba5/ffff drop, //Drop internalnet source
                                9/01 9,
                                9/06 20/01bb 0,
                                9/06 20/0016 0,
                                 9/06                    20/0015 0,
                                      9/06               20/0050 0,
												 9/06	22/0050 0,
                                           9/06          22/01bb 0,
	 9/06												22/0016 0,
          9/06                                           22/0015 0,
       9/17 20/0035 0,
       9/17                        22/0035 0,
                                                    - 0) ->  [0]output
            }
fwIN :: FwIN()


router :: CTXDispatcher(  16/8ba5/ffff 0, - 0)

router[0] -> tdIN


fdIN -> fcIN :: FlowClassifier(VERBOSE 1, BUILDER $builder, AGGCACHE $aggcache, CACHESIZE 0, CACHERINGSIZE 8) -> dispIN [0] -> FlowStrip(14) -> chIN :: CheckIPHeader(CHECKSUM false) -> fwIN [0] -> avgIN :: AverageCounter() ->  router
dispIN[1] -> Print("Pass through") -> Discard
dispIN[2] -> Print("UNKNOWN INPUT TRAFFIC") -> Discard()


chIN[1] -> Print("Bad IP") -> Discard

//####################
//FIREWALL configurations
//####################
TSCClock(INSTALL true, NOWAIT true);
JiffieClock();

Script(print "MAC address :", print $(fdIN/f.mac))

//Learn MAC
FastUDPFlows(RATE 1, LIMIT -1, LENGTH 60, SRCETH $INsrcmac, DSTETH ff:ff:ff:ff:ff:ff, SRCIP $INsrcip, DSTIP $INdstip, FLOWS 1, FLOWSIZE 1) -> MarkMACHeader -> RatedUnqueue(1) -> Print(ADVERTISE) -> tIN


DriverManager(  pause,
    print "Count : $(avgIN.count)/sw$(fdIN/f.count)/hw$(fdIN/f.hw_count)",
    print "Receive rate : $(avgIN.link_rate)",
    print "Dropped : $(dropIN.count)",

    print "Sent : $(tIN.n_sent)",
    print "Firewalled : $(fwIN.dropped)",
//      print "Median : $(batchin.median), Avg : $(batchin.average), \nDump : $(batchin.dump)"
    )

//############################
