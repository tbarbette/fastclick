#ifndef CLICK_STATEFULTRANSLATOR64_HH_
#define CLICK_STATEFULTRANSLATOR64_HH_
#include <click/batchelement.hh>
#include <click/bighashmap.hh>
#include <click/ip6flowid.hh>
CLICK_DECLS

/*
 * =c
 * StatefulTranslator64()
 * =s ip6
 *
 * =d
 * Translates IPv6 packets into IPv4 packets as defined in RFC6146. IPv4 packets are translated back to IPv6 iff they qualify as the return
 * traffic to an already established 6-to-4 flow.
 *
 * Each port expects the packet offset to be at the network header. Port 0 expects valid IPv6, emits valid IPv4 (updated checksum). Port 1
 * expects valid IPv4, emits valid IPv6 (updated checksum).
 *
 * SA  (Source Address), SP (Source Port), DA (Destination Address), DP (Destination Port). The inverse character ' next to any of these
 * indicates the translated form.
 *
 * The translation is similar to port address translation (PAT), many(v6)-to-one(ipv4). Hence the translation is stateful, for which the
 * element maintains two key-value maps.
 *	(1) _map64: Key=(SA, SP, DA, DP) to Value=(SA', SP')
 *	(2) _map46: Key=(DA, DP, SA', SP') to Value(SA, SP)
 *
 * =n
 * At the current state of the implementation, the destination v4 address has to be embedded in a v6 well-known prefix (WKP) for it to be
 * considered fit for translating from v6 to v4.
 *
 * See https://tools.ietf.org/html/rfc6146#section-1.2.2 for a detailed use-case.
 *
 * =e
 * For example
 * ----Initiating traffic from v6 to v4----
 * FromDevice
 * 		-> Strip(14)
 * 		-> CheckIP6Header
 * 		-> StatefulTranslator64
 * 		-> EtherEncap(0x0800,.......)
 * 		-> ToDevice;
 *
 * ----Return traffic from v4 to v6----
 * FromDevice
 * 		-> Strip(14)
 * 		-> CheckIPHeader
 * 		-> [1]StatefulTranslator64[1]
 * 		-> EtherEncap(0x86DD,.......)
 * 		-> ToDevice;
 *
 * =a
 * ProtocolTranslator64, ProtocolTranslator46*/

typedef union {
	IP6Address _v6;
	IPAddress _v4;
} netAddress;

class StatefulTranslator64 : public BatchElement {
public:
	class Mapping;
	typedef HashMap<IP6FlowID, Mapping *> Map6;
	typedef HashMap<IPFlowID, Mapping *> Map4;
	IPAddress mappedv4Address;

	StatefulTranslator64();
	~StatefulTranslator64();

	const char *class_name() const { return "StatefulTranslator64"; }
	const char *port_count() const { return "2/1-2"; }
	const char *processing() const { return PUSH; }
	int configure(Vector<String> & , ErrorHandler *) CLICK_COLD;
	void push(int port, Packet *p);

#if HAVE_BATCH
	void push_batch(int port, PacketBatch *batch) override;
#endif

private:
	Map6 _map64;
	Map4 _map46;
	uint16_t _nextPort;
	Packet* sixToFour(Packet *p);
	Packet* fourToSix(Packet *p);
	Packet* translate64(Packet *p, const click_ip6 *v6l3h, const click_tcp *l4h, Mapping *addressAndPort);
	Packet* translate46(Packet *p, const click_ip *v4l3h, const click_tcp *l4h, Mapping *addressAndPort);
};

class StatefulTranslator64::Mapping {

	union address{
		IP6Address _v6;
		IPAddress _v4;
		address() {memset(this,0,sizeof(address));
		};
	};

public:
	Mapping() CLICK_COLD;
	void initializeV4(const IPAddress &address, const unsigned short &port) { mappedAddress._v4 = address; _mappedPort = port;}
	void initializeV6(const IP6Address &address, const unsigned short &port) { mappedAddress._v6 = address; _mappedPort = port;}

protected:
	address mappedAddress;
	unsigned short _mappedPort;
	friend class StatefulTranslator64;
};

CLICK_ENDDECLS
#endif
