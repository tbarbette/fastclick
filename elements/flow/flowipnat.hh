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



class IPPair {
  public:

    IPAddress src;
    IPAddress dst;
    IPPair() {
        src = 0;
        dst = 0;
    }
    IPPair(IPAddress a, IPAddress b) {
        src = a;
        dst = b;
    }

    inline hashcode_t hashcode() const {
       return CLICK_NAME(hashcode)(src) + CLICK_NAME(hashcode)(dst);
   }

   inline bool operator==(IPPair other) const {
       return (other.src == src && other.dst == dst);
   }
};

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
typedef HashTableMP<NATEntry,IPPair> LBHashtable;
#else
typedef HashTable<NATEntry,IPPair> LBHashtable;
#endif

class FlowIPNAT : public FlowBufferElement<IPPair> {

public:

    FlowIPNAT() CLICK_COLD;
    ~FlowIPNAT() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPNat"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, IPPair*, PacketBatch *);

    LBHashtable _map;
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

    friend class FlowIPLoadBalancerReverse;

private:

    FlowIPNAT* _in;
};

CLICK_ENDDECLS
#endif
