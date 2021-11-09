elementclass IP6Input{
    input
    -> Strip(14)
	-> CheckIP6Header(PROCESS_EH true)
    -> l :: LookupIP6Route(ff02::2/128 ::0 1, ::0/0 ::0 0)
	-> DecIP6HLIM()
    -> output;

    l[1] -> IP6Print("Router broadcast discarded...") -> Discard();
};

elementclass InputDecap { $port, $src, $dst, $ip6src |

	input
	-> c :: Classifier(12/86dd 20/3a 54/87, 12/86DD, -);

	c[0]
	-> MarkIP6Header(OFFSET 14)
	-> IP6Print("NDP")
	-> IP6NDAdvertiser($ip6src $src)
	-> [1]output;

	c[1]
    //-> Print("IP6 from $port", -1)
    -> IP6Input()
//	-> IP6Print("IP6 from port $port")
	-> output;

    c[2] -> Print("Non-IPv6") -> Discard;
};



elementclass InputEncap { $port, $src, $dst, $ip6src, $noencap |
    input
	-> c :: Classifier(12/86dd 20/3a 54/87, 12/86DD, 12/0800, 12/0806,-);

	c[0]
	-> MarkIP6Header(OFFSET 14)
	-> IP6Print("NDP")
	-> IP6NDAdvertiser($ip6src $src) -> [1]output;

	c[1]
	//-> Print("IP6 from $port", -1)
	-> IP6Input()
//	-> IP6Print("IP6 from port $port")
    -> {
        [0] -> s :: Switch($noencap);
            s[0] -> IP6SREncap(ADDR fc00::9, ADDR fc00::a, ENCAP_DST true) -> [0];
            s[1] -> [0];
    }	
	-> output;

    c[2] -> Print("IPv4 (discarded)")
	-> Strip(14)
	-> CheckIPHeader()
	-> Discard;

    c[3] -> Print("ARP (discarded)")
	-> Discard;

    c[4] -> Print("Non-IPv6") -> Discard;
};


elementclass Output { $src, $dst |
	input
		-> EtherEncap(0x86DD, SRC $src, DST $dst)
		-> output;
}
