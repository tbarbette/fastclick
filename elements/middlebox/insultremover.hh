#ifndef MIDDLEBOX_INSULTREM_HH
#define MIDDLEBOX_INSULTREM_HH
#include <click/element.hh>
#include "stackelement.hh"

CLICK_DECLS

class InsultRemover : public StackElement
{
public:
    InsultRemover() CLICK_COLD;

    const char *class_name() const        { return "InsultRemover"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

protected:
    Packet* processPacket(struct fcb*, Packet*);
    int packetNb = 0;
};

CLICK_ENDDECLS
#endif
