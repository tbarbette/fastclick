///////////////////////////////////////////////////////////////////////////////////////////
// Deep Packet Inspection using Snort-like payload matching
//
// Dummy version, with only a single pattern : "attack"
//
///////////////////////////////////////////////////////////////////////////////////////////
elementclass DPI {

	// We go for IPv4
	classifier0 :: Classifier(12/0800, -);

	// IDS logic is symmetric on both sides of the pipeline
	regexClassifier0::RegexMatcherMP(attack, PAYLOAD_ONLY true);

	// regexClassifier0::StringMatcherMP(attack, PAYLOAD_ONLY true);

	/////////////////////////////////////////////////////////////////////
	// Processing pipeline
	/////////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////                      
	// Port 0 -> Port 1
	///////////////////////////////////////////////////
	input[0] -> classifier0;

	// IPv4 traffic is deeply inspected
	classifier0[0] -> [0]regexClassifier0;
	// Non-IPv4 traffic is discarded
	classifier0[1] -> Print("Non-IP-Port0: Discard") -> Discard;

    dropIN:: Print(DROPIN) -> Discard;

	regexClassifier0[0]->dropIN;
	regexClassifier0[1]->[0]output;

}
