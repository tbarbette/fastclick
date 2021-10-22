#ifndef CLICK_SetTransportChecksumIP6_HH
#define CLICK_SetTransportChecksumIP6_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
CLICK_DECLS

/*
 * =c
 * SetTransportChecksumIP6()
 * =s ip6
 * sets IP6 tansport packets' checksums
 * =d
 * Input packets should be TCP/UDP/ICMPv6 in IP6. The transport header and the IP6_NXT annotation should be set, typically using CheckIP6Header.
 *
 * Calculates the TCP/UDP/ICMPv6 header's checksum and sets the checksum header field.
 * Uses the IP6 header fields to generate the pseudo-header.
 *
 * Example:
 *
 * FromDump("dump.pcap") -> CheckIP6Header(OFFSET 14) -> SetTransportChecksumIP6 -> ToDump("dump-with-ipv6-tcp-cksum.pcap", UNBUFFERED true);
 *
 * =a CheckIP6Header, SetTCPChecksum
 */

class SetTransportChecksumIP6 : public SimpleElement<SetTransportChecksumIP6> { public:

  SetTransportChecksumIP6() CLICK_COLD;
  ~SetTransportChecksumIP6() CLICK_COLD;

  const char *class_name() const override		{ return "SetTransportChecksumIP6"; }
  const char *port_count() const override		{ return PORTS_1_1; }
  int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;

  Packet *simple_action(Packet *);
};

CLICK_ENDDECLS
#endif
