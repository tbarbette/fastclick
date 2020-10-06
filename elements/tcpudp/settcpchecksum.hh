#ifndef CLICK_SETTCPCHECKSUM_HH
#define CLICK_SETTCPCHECKSUM_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
CLICK_DECLS

/*
 * =c
 * SetTCPChecksum([FIXOFF])
 * =s tcp
 * sets TCP packets' checksums
 * =d
 * Input packets should be TCP in IP.
 *
 * Calculates the TCP header's checksum and sets the checksum header field.
 * Uses the IP header fields to generate the pseudo-header.
 *
 * =a CheckTCPHeader, SetIPChecksum, CheckIPHeader, SetUDPChecksum
 */

class SetTCPChecksum : public SimpleElement<SetTCPChecksum> { public:

  SetTCPChecksum() CLICK_COLD;
  ~SetTCPChecksum() CLICK_COLD;

  const char *class_name() const override		{ return "SetTCPChecksum"; }
  const char *port_count() const override		{ return PORTS_1_1; }
  int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;

  Packet *simple_action(Packet *);

private:
  bool _fixoff;
};

CLICK_ENDDECLS
#endif
