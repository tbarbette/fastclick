#ifndef CLICK_SETTCPCHECKSUMIP6_HH
#define CLICK_SETTCPCHECKSUMIP6_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
CLICK_DECLS

/*
 * =c
 * SetTCPChecksumIP6()
 * =s tcp
 * sets TCP/IP6 packets' checksums
 * =d
 * Input packets should be TCP in IP6.
 *
 * Calculates the TCP header's checksum and sets the checksum header field.
 * Uses the IP6 header fields to generate the pseudo-header.
 *
 * Example:
 *
 * FromDump("dump.pcap") -> CheckIP6Header(, 14) -> SetTCPChecksumIP6 -> ToDump("dump-with-ipv6-tcp-cksum.pcap", UNBUFFERED true);
 *
 * =a CheckIP6Header, SetTCPChecksum
 */

class SetTCPChecksumIP6 : public SimpleElement<SetTCPChecksumIP6> { public:

  SetTCPChecksumIP6() CLICK_COLD;
  ~SetTCPChecksumIP6() CLICK_COLD;

  const char *class_name() const override		{ return "SetTCPChecksumIP6"; }
  const char *port_count() const override		{ return PORTS_1_1; }
  int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;

  Packet *simple_action(Packet *);
};

CLICK_ENDDECLS
#endif
