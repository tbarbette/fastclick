//Definitions
define($leftif eth1)
define($rightif eth2)
define($tofif eth3)

define($right_mac 00:00:00:00:00:01)
define($rightif_mac 52:54:00:b7:a8:bd)

define($left_mac 00:00:00:00:00:03)
//define($leftif_mac 52:54:00:7b:be:f9)
define($leftif_mac 00:00:00:00:00:01)

define($ping_dst 8.8.8.8)

define($tofif_ip 20.20.20.5)
define($tofif_gw 20.20.20.1)
define($tofif_mac 00:aa:aa:aa:aa:dd)

define($tofapp_ip 172.24.4.152)
//define($tofapp_mac aa:aa:aa:aa:cc:bb)


torightq :: Queue;

//From eNodeB
FromDevice($leftif, PROMISC true, SNIFFER false)
	-> Print("FROM LEFT",-1, ACTIVE $verbose)
	-> flc :: Classifier(12/0800, 12/0806, -)
	-> Strip(14)
	-> chleft :: CheckIPHeader
	-> IPPrint("FROM LEFT", ACTIVE $verbose)
	-> gtpclassleft :: IPClassifier(dst udp port 2152,-)
	-> Strip(36) // IP + UDP + GTP
	-> MarkIPHeader
	-> IPPrint("FROM LEFT INSIDE")
	-> classifier :: IPClassifier(ip dst 10.200.205.206, -);

gtpclassleft[1]
	-> Print("LEFT NON GTP", ACTIVE $verbose) -> IPPrint("LEFT NON GTP", ACTIVE $verbose)
	-> Unstrip(14)
	-> torightq;

flc[1] -> Print("LEFT ARP", ACTIVE $verbose) -> torightq;

flc[2] -> Print("LEFT OTHER", ACTIVE $verbose) -> torightq;

//To TOF rules
classifier[0]
	-> Unstrip(36)
	-> MarkIPHeader()
	-> Print(TOTOF, -1, ACTIVE $verbose)
	-> gtptable :: GTPTable(PING_DST $ping_dst)
	-> chtof :: CheckIPHeader
	-> IPPrint("TOTOF Ip", ACTIVE $verbose)
	-> tofrw :: IPClassifier(proto icmp && type echo,-);

rewrited :: Null;

tofrw[1]
	-> Print(IP, ACTIVE $verbose)
	-> tofiprw :: IPRewriter(pattern $tofif_ip - $tofapp_ip - 0 1)
	-> rewrited;

tofrw[0] -> Print("TO TOF ICMP", ACTIVE $verbose)
	-> toficmprw :: ICMPPingRewriter(pattern $tofif_ip $tofapp_ip 0 1)
	-> Print("TO TOF ICMP rewritten", ACTIVE $verbose) -> rewrited;
	//-> SetIPAddress($tofapp_ip) //Should be set by rule

rewrited
	-> IPPrint("TOTOF Rewrited", ACTIVE $verbose)
	//-> EtherEncap(ETHERTYP 0x0800, SRC $tofif_mac, DST $tofapp_mac)
	-> SetIPAddress($tofif_gw)
	-> tofarpq :: ARPQuerier(IP $tofif_ip, ETH $tofif_mac)
	-> Print("TOTOF ARP",-1, ACTIVE $verbose)
	-> totofq :: Queue
	-> totofd :: ToDevice($tofif);

chtof[1] -> Print("TOF does not decap IPs? Discarded.", ACTIVE $verbose) -> Discard;

//Passthrough (as GTP)
classifier[1]
	-> Unstrip(36)
	-> Unstrip(14)
	-> torightq
	-> ToDevice($rightif);

gtptable[1]
	-> Print("EMITTING",-1, ACTIVE $verbose)
	-> chemit :: CheckIPHeader()
	-> IPPrint("EMITTED ICMP", ACTIVE $verbose)
	-> CheckUDPHeader()
	-> EtherEncap(ETHERTYPE 0x0800, SRC $rightif_mac, DST $right_mac)
	-> Print("EMITTED ETHER", -1, ACTIVE $verbose)
	-> torightq;

//From Core
FromDevice($rightif, PROMISC true, SNIFFER false)
	-> Print("FROM RIGHT", -1, ACTIVE $verbose)
	-> frc :: Classifier(12/0800, 12/0806, -)
	-> Strip(14)
	-> chright :: CheckIPHeader
	-> IPPrint("FROM RIGHT IP", ACTIVE $verbose)
	-> gtpclassright :: IPClassifier(dst udp port 2152,-)
	-> Strip(28)
	-> GTPDecap()
	-> Print("FROM RIGHT DECAPED", ACTIVE $verbose)
	-> chrightin :: CheckIPHeader
	-> icmpret :: IPClassifier(proto icmp && type echo-reply && ip src $ping_dst , -);


toleftq :: Queue;

gtpclassright[1]
	-> Print("RIGHT NON GTP", ACTIVE $verbose) -> IPPrint("RIGHT NON GTP", ACTIVE $verbose)
	-> Unstrip(14)
	-> toleftq;

icmpret[0]
	-> IPPrint("RETURNED ICMP!", ACTIVE $verbose) -> Unstrip(36) -> MarkIPHeader() -> [1]gtptable;
icmpret[1]
	-> GTPEncap(0) //Encap using ANNO
	-> Unstrip(28)
	-> Unstrip(14)
	-> toleftq
	-> toleft :: ToDevice($leftif);

frc[1]
	-> Print("RIGHT ARP", ACTIVE $verbose) -> toleftq;
frc[2]
	-> Print("RIGHT OTHER", ACTIVE $verbose) -> toleftq;

//From TOF IF
FromDevice($tofif, PROMISC true, SNIFFER false, HEADROOM 64)
	-> ctof :: Classifier(12/0800, 12/0806 20/0001, 12/0806 20/0002);
ctof[1]
	-> Print("TOF ARP REQUEST", ACTIVE $verbose)
	-> ARPResponder($tofif_ip $tofif_mac)
	-> totofq;
ctof[2]
	-> Print("TOF ARP RESPONSE", ACTIVE $verbose)
	-> [1]tofarpq;

ctof[0]
	-> Print("FROM TOF", ACTIVE $verbose)
	-> Classifier(12/0800)
	-> Strip(14)
	-> CheckIPHeader
	-> tofbackrwc :: IPClassifier(proto icmp && type echo-reply,-);

fromtoforig :: Null;
tofbackrwc[1]
	-> [0]tofiprw[1]
	-> Print("FROM TOF REWRITED", ACTIVE $verbose)
	-> IPPrint("FROM TOF IP REWRITTEN", ACTIVE $verbose)
	-> fromtoforig;
tofbackrwc[0]
	-> [0]toficmprw[1]
	-> Print("FROM TOF ICMP REWRITTEN", ACTIVE $verbose)
	-> IPPrint("FROM TOF ICMP REWRITTEN", ACTIVE $verbose)
	-> fromtoforig;

fromtoforig
	-> gtplookup :: GTPLookup(TABLE gtptable)
	-> MarkIPHeader
	-> IPPrint("TOFED TO LEFT", ACTIVE $verbose)
	-> EtherEncap(SRC $leftif_mac, DST $left_mac, ETHERTYPE 0x0800)
	-> Print("TOFED TO LEFT",-1, ACTIVE $verbose)
	-> toleftq;

HTTPServer();
