#ifndef CLICK_CHECKBIERIN6HEADER_HH
#define CLICK_CHECKBIERIN6HEADER_HH
#include <click/batchelement.hh>
#include <click/atomic.hh>
CLICK_DECLS

/*
 * =c
 * CheckBIERin6Header([OFFSET])
 * =s ip6
 *
 * =d
 *
 * Expects BIER packets encapsulated in IPv6 packets as input starting at OFFSET bytes. Default OFFSET is zero.
 * Checks that the IPv6 packet contains a BIER packet.
 * Pushes invalid or non BIERin6 packets out on output 1, unless output 1 was unused; if so,
 * drop the packets.
 *
 * Keyword arguments are:
 *
 * =over 8
 *
 * =item OFFSET
 *
 * Unsigned integer. Byte position at which the BIER header begins. Default is 0.
 *
 * =back
 *
 */
class CheckBIERin6Header : public SimpleElement<CheckBIERin6Header> {
  public:
   CheckBIERin6Header();
   ~CheckBIERin6Header();

   const char *class_name() const override { return "CheckBIERin6Header"; }
   const char *port_count() const override { return PORTS_1_1X2; }

   int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

   Packet *simple_action(Packet *p);

  private:
    int _offset;
    atomic_uint64_t _drops;
    void drop(Packet *p); 
};

CLICK_ENDDECLS
#endif
