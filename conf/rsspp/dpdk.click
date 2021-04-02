define( 		  $DPDKIF 0,
                  $VERBOSE 0,
                  $TIMER 1000);


fd0 :: FromDPDKDevice($DPDKIF, RSS_AGGREGATE 1)
          -> agg :: AggregateCounterVector(MASK 511)
          -> EtherMirror
          -> ToDPDKDevice($DPDKIF);
    
balancer :: DeviceBalancer(DEV fd0, METHOD rsspp, VERBOSE $VERBOSE, TIMER $TIMER, STARTCPU 4, RSSCOUNTER agg, AUTOSCALE 0);
