#ifndef CLICK_CHECKBIERHEADER_HH
#define CLICK_CHECKBIERHEADER_HH
#include <click/batchelement.hh>
#include <click/confparse.hh>
#include <clicknet/bier.h>
CLICK_DECLS

/*
 * =c
 * CheckBIERHeader([OFFSET])
 * =s ip6
 *
 * =d
 * 
 * Expects BIER packets as input starting at OFFSET bytes. Default OFFSET is zero.
 * Checks that thet packet's length is reasonable.
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

class CheckBIERHeader : public SimpleElement<CheckBIERHeader> {
  public:
     CheckBIERHeader();
     ~CheckBIERHeader();

     const char *class_name() const override { return "CheckBIERHeader"; }
     const char *port_count() const override { return PORTS_1_1X2; }
     const char *processing() const override { return PROCESSING_A_AH; }

     int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
     void add_handlers() CLICK_COLD;

     Packet *simple_action(Packet *p);

  private:
    int _offset;
    atomic_uint64_t _drops;

    enum {h_drops};
    
    void drop(Packet *p);
    static String read_handler(Element *e, void *thunk) CLICK_COLD;
  
};

CLICK_ENDDECLS
#endif
