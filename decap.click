require(library mec.conf)

tun :: KernelTun($EXTNET_SRV, HEADROOM 48, DEVNAME tunnelin)
-> CheckIPHeader()
-> Print(TOENCAP, -1)
-> IPPrint
-> GTPEncap(60)
-> UDPIPEncap($RIGHT, 5050, $LEFT, 2152)
-> CheckIPHeader
-> Queue
-> Print(ENCAPED, -1)
-> EtherEncap(0x0800, aa:aa:aa:aa:bb:bb, aa:aa:aa:aa:bb:aa)
-> IPPrint
-> ToDevice(ens10)


FromDevice(ens10, PROMISC true)
-> Print(IN,-1)
-> Classifier(12/0800)
-> Strip(14)
-> chip :: CheckIPHeader
-> IPPrint("IN IP")
-> ipc :: IPClassifier(proto udp, -)
-> Print(TODECAP, -1)
-> IPPrint
-> Strip(28)
-> GTPDecap()
-> chipin :: CheckIPHeader()
-> Print(DECAPED, -1)
-> IPPrint
-> tun


chip[1] -> Print("BAD IP IN")-> Discard;
chipin[1] -> Print("BAD SECOND IP IN")-> Discard;
ipc[1] -> Print("NOT UDP") -> Discard;
