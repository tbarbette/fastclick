///////////////////////////////////////////////////////////////////////////////////////////
// L3-L4 Firewall
///////////////////////////////////////////////////////////////////////////////////////////

// Comment: Feel free to get rid of all the unnecessary arguments of this class.
// I did the same in DPI.

elementclass Firewall {

	// L2 encapsulation after processing
	etherEncap :: EtherEncap(0x0800, $macAddr1, $gwMACAddr0);

	// Classifier
	classifier :: Classifier(
		12/0800,           /* IPv4 packets    */
		-                  /* Everything else */
	);

	// Strip Ethernet header
	strip :: Strip(14);

	// Check IP header
	checkIPHeader :: CheckIPHeader(INTERFACES $ipNet0 $ipNet1, VERBOSE true);

	
	/////////////////////////////////////////////////////////////////////
	// Processing pipeline INPUT
	/////////////////////////////////////////////////////////////////////
	// Send input frame to the routing table
	// Classify inputs frames as follows
	input[0] -> classifier;

	// Classify packets
	// --> IPv4 proceeds
	classifier[0] -> Paint(1) -> strip;

	// --> Drop irrelevant packets
	classifier[1] -> Discard;

	// IPv4 Processing
	strip
		-> checkIPHeader
		-> etherEncap

	// Drop the traffic that does not match any rule
	filter[1] -> IPPrint(IN) -> Discard;

	// Output of this NF
	etherEncap -> [0]output;


	/////////////////////////////////////////////////////////////////////
}
