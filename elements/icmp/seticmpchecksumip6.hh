#ifndef CLICK_SetICMPChecksumIP6_HH
#define CLICK_SetICMPChecksumIP6_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
CLICK_DECLS

/*
 * =c
 * SetICMPChecksumIP6()
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
 * FromDump("dump.pcap") -> CheckIP6Header(, 14) -> SetICMPChecksumIP6 -> ToDump("dump-with-ipv6-tcp-cksum.pcap", UNBUFFERED true);
 *
 * =a CheckIP6Header, SetTCPChecksum
 */

class SetICMPChecksumIP6 : public SimpleElement<SetICMPChecksumIP6> { public:

  SetICMPChecksumIP6() CLICK_COLD;
  ~SetICMPChecksumIP6() CLICK_COLD;

  const char *class_name() const override		{ return "SetICMPChecksumIP6"; }
  const char *port_count() const override		{ return PORTS_1_1; }
  int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;

  Packet *simple_action(Packet *);
};

CLICK_ENDDECLS
#endif
