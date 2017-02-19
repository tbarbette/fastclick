require(package "openbox");
input_cap::FromDump(test_load_balance.pcap, STOP true);
output_match1::ToDump(output_xnet.pcap, SNAPLEN 0, ENCAP ETHER);
output_match2::ToDump(output_ynet.pcap, SNAPLEN 0, ENCAP ETHER);
output_match3::ToDump(output_games.pcap, SNAPLEN 0, ENCAP ETHER);
output_match4::ToDump(output_test.pcap, SNAPLEN 0, ENCAP ETHER);
output_match5::ToDump(output_rest.pcap, SNAPLEN 0, ENCAP ETHER);

eth_classifier::Classifier(12/0800, -);
classifier::StringClassifier("xnet\x02", "ynet", "games", "test");
input_cap
	-> eth_classifier
	-> CheckIPHeader(14)
	-> classifier;
eth_classifier[1] -> Discard;
classifier[0] -> output_match1;
classifier[1] -> output_match2;
classifier[2] -> output_match3;
classifier[3] -> output_match4;
classifier[4] -> output_match5;

