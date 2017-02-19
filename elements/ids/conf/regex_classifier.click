require(package "openbox");
input_cap::FromDump(test_load_balance.pcap, STOP true);
output_match1::ToDump(output_match1.pcap, SNAPLEN 0, ENCAP ETHER);
output_match2::ToDump(output_match2.pcap, SNAPLEN 0, ENCAP ETHER);
eth_classifier::Classifier(12/0800, -);
regex_classifier::RegexClassifier(xnet, ".*test2.*", ".*");
input_cap
	-> eth_classifier
	-> CheckIPHeader(14)
	-> regex_classifier;
eth_classifier[1] -> Discard;
regex_classifier[0] -> output_match1;
regex_classifier[1] -> output_match2;
regex_classifier[2] -> Discard;
