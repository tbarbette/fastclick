#ifndef CLICK_FLOWIPNAT_HH
#define CLICK_FLOWIPNAT_HH
#include <click/config.h>
#include <click/flowelement.hh>
#include <click/multithread.hh>
#include <click/hashtablemp.hh>
#include <click/glue.hh>
#include <click/vector.hh>
CLICK_DECLS
/*

class FlowIPNAT : public FlowBufferElement<int> {

public:

    FlowIPNAT() CLICK_COLD;
    ~FlowIPNAT() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPNat"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, int*, PacketBatch *);

    HashTableMP<NATEntry> _map;
private:
    IPAddress _sip;
};

class FlowIPNATReverse : public FlowBufferElement<IPAddress> {

public:

    FlowIPNATReverse() CLICK_COLD;
    ~FlowIPNATReverse() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPNATReverse"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, IPAddress*, PacketBatch *);

private:

    FlowIPNAT* _entry;
};
*/
CLICK_ENDDECLS
#endif
