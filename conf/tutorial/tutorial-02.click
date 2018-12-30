define ($ip 10.100.0.2,
        $mac aa:aa:aa:aa:aa:aa)

Idle -> tap :: KernelTap(10.100.0.1/24, DEVNAME vEth1) ; 
tap -> c :: Classifier(12/0806 20/0001, 12/0806 20/0002, 12/0800, -);

//Answer to ARP requests
c[0] -> ARPResponder( $ip  $mac)  -> tap ;
//Discard ARP replies for now
c[1] -> Discard;
//Print IP packets
c[2] -> Print("IP", -1) -> Discard;
//Discard non-IP, non-ARP packets
c[3] -> Discard;

