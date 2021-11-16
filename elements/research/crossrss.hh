#ifndef CLICK_CROSSRSS_HH
#define CLICK_CROSSRSS_HH
#include <click/config.h>
#include <click/tcphelper.hh>
#include <click/multithread.hh>
#include <click/glue.hh>
#include <click/loadbalancer.hh>
#include <click/vector.hh>

#include <click/flow/flowelement.hh>

#define LB_FLOW_TIMEOUT 60 * 1000

CLICK_DECLS

struct CrossRSSEntry {
    int chosen_server;
    CrossRSSEntry(int addr) : chosen_server(addr) {
    }

};


/**
=c

CrossRSS([I<KEYWORDS>])

=s flow

Simulator for CrossRSS. See the CoNEXT Poster.

=d

Load-balancer among Click ports.

Keyword arguments are:

=over 8


=back

=e
    fl :: CrossRSS(MODE RR);
    fl[0] -> ...;
    fl[1] -> ...;

=a

FlowIPLoadBalancer */

struct CPULoad {
    CPULoad() : raw(0), load(0) {
    }

    int raw;
    int load;
};

struct CPUMachineLoad {
    Vector<CPULoad> cores;
    Vector<int> map;
};

class CrossRSS : public FlowStateElement<CrossRSS,CrossRSSEntry>,
                           public TCPHelper, public LoadBalancer<IPAddress> {
    public:
        CrossRSS() CLICK_COLD;
        ~CrossRSS() CLICK_COLD;

        const char *class_name() const { return "CrossRSS"; }
        const char *port_count() const { return "1/1-"; }
        const char *processing() const { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *errh) override CLICK_COLD;

        int get_core_for_hash(int hash, int id, int cores_per_server);
        static const int timeout = LB_FLOW_TIMEOUT;
        bool new_flow(CrossRSSEntry*, Packet*);
        void release_flow(CrossRSSEntry*) {};

        void push_flow(int, CrossRSSEntry*, PacketBatch *);

        virtual void add_handlers() override CLICK_COLD;
    private:
        enum { h_cpu_load = LoadBalancer::h_lb_max, h_cross_rehash };


        Vector<CPUMachineLoad> _machines;
        int _table_size;
        int _cores_per_server;
    public:
        static int write_handler(const String &in_str, Element *e, void *thunk, ErrorHandler *errh);
        static int handler(int op, String& s, Element* e, const Handler* h, ErrorHandler* errh);
        static String read_handler(Element *e, void *thunk);
};

CLICK_ENDDECLS
#endif
