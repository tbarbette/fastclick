#ifndef CLICK_FLOWIPNAT_HH
#define CLICK_FLOWIPNAT_HH
#include <click/config.h>
#include <click/flowelement.hh>
#include <click/multithread.hh>
#include <click/hashtablemp.hh>
#include <click/glue.hh>
#include <click/vector.hh>
CLICK_DECLS
#define IPLOADBALANCER_MP 1
#if IPLOADBALANCER_MP
#include <click/hashtablemp.hh>
#else
#include <click/hashtable.hh>
#endif
CLICK_DECLS

struct NATEntry {
    IPAddress dest;
    uint16_t port;
    NATEntry(IPAddress addr, uint16_t port) : dest(addr), port(port) {

    }
    inline hashcode_t hashcode() const {
       return CLICK_NAME(hashcode)(dest) + CLICK_NAME(hashcode)(port);
   }

   inline bool operator==(NATEntry other) const {
       return (other.dest == dest && other.port == port);
   }

};
#if IPLOADBALANCER_MP
typedef HashTableMP<NATEntry,IPPair> NATHashtable;
#else
typedef HashTable<NATEntry,IPPair> NATHashtable;
#endif

class FlowIPNAT : public FlowSpaceElement<IPPair> {

public:

    FlowIPNAT() CLICK_COLD;
    ~FlowIPNAT() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPNat"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, IPPair*, PacketBatch *);

    NATHashtable _map;
private:
    IPAddress _sip;
};

class FlowIPNATReverse : public FlowSpaceElement<IPPair> {

public:

    FlowIPNATReverse() CLICK_COLD;
    ~FlowIPNATReverse() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPNATReverse"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    //int initialize(ErrorHandler *errh);

    void push_batch(int, IPPair*, PacketBatch *);

    friend class FlowIPLoadBalancerReverse;

private:

    FlowIPNAT* _in;
};

CLICK_ENDDECLS
#endif
