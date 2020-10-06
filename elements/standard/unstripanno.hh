#ifndef CLICK_UNSTRIPANNO_HH
#define CLICK_UNSTRIPANNO_HH
#include <click/element.hh>
CLICK_DECLS

/*
 * =c
 * UnstripAnno(LENGTH)
 * =s basicmod
 * Unstrip the number of bytes specified by an annotation from front of packets
 * =d
 * Put the bytes specified in an annotation at the front of the packet. These bytes may be bytes
 * previously removed by StripTCPHeader.
 *
 * =a StripTCPHeader
 */

class UnstripAnno : public Element {

 public:

  UnstripAnno();

  const char *class_name() const override	{ return "UnstripAnno"; }
  const char *port_count() const override	{ return PORTS_1_1; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

  Packet *simple_action(Packet *);

private:
  int _anno;
};

CLICK_ENDDECLS
#endif
