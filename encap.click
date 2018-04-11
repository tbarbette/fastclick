require(library mec.conf)

tun :: KernelTun($EXTNET, HEADROOM 48, DEVNAME tunnelin)
-> CheckIPHeader()
-> Print(TOENCAP, -1)
-> IPPrint("TOENCAP IP")
-> GTPEncap(50)
-> UDPIPEncap($LEFT, 5050, $RIGHT, 2152)
-> CheckIPHeader
-> EtherEncap(0x0800, aa:aa:aa:aa:bb:aa, aa:aa:aa:aa:bb:bb)
-> Queue
-> Print(ENCAPED, -1)
-> IPPrint("ENCAPED IP")
-> ToDevice(ens10)


FromDevice(ens10, PROMISC true)
-> c :: Classifier(12/0800, -)
-> Strip(14)
-> CheckIPHeader
-> IPClassifier(proto udp)
-> Print(TODECAP, -1)
-> IPPrint
-> Strip(28)
-> GTPDecap()
-> CheckIPHeader
-> Print(DECAPED, -1)
-> IPPrint
-> tun

c[1] -> Print("NOT IP", -1) -> Discard;
