/**
 * ARPHandler
 *
 * Handle all ARP subsystem and passes only IP packets on output 1.
 * Similarly, IP packets should be passed in input 1 and ARPHandler will
 * take care of the Ethernet encapsulation
 * 
 * input[0] should receive packets from the device, and output[0] connected
 * directly to the same device.
 *
 * Eg. :
 * FromDPDKDevice(0) -> ARPHandler(10.0.0.2, 5B:EC:DE:27:A8:B2);
 *   arp[0] -> ToDPDKDevice(0);
 *   arp[1] -> Print("IP PACKET") -> SetIPAddress(10.0.0.1) -> arp[1];
 */
elementclass ARPHandler { $ip, $mac |
        input[0] -> c :: Classifier(12/0806 20/0001, // ARP request
                                 12/0806 20/0002, // ARP response
                                 12/0800);        // IP packets
        c[1] -> [1]arpQ :: ARPQuerier($ip, $mac) -> output[1];
        c[0] -> [0]arpR :: ARPResponder($ip $mac);

        arpQ->output[0];
        c[2]->output[1];

        input[1] -> [0]arpQ;
}
