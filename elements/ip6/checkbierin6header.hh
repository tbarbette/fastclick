#ifndef CLICK_CHECKBIERIN6HEADER_HH
#define CLICK_CHECKBIERIN6HEADER_HH
#include <click/batchelement.hh>
#include <click/atomic.hh>
CLICK_DECLS

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
