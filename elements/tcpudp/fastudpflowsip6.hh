#ifndef FastUDPFlowsIP6_HH
#define FastUDPFlowsIP6_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
#include <click/gaprate.hh>
#include <click/packet.hh>
#include <click/ip6address.hh>
#include <clicknet/ether.h>
#include <clicknet/udp.h>

CLICK_DECLS

/*
 * =c
 * FastUDPFlowsIP6(RATE, LIMIT, LEN,
 *              SRCETH, SRCIP,
 *              DSTETH, DSTIP,
 *              FLOWS, FLOWSIZE [, CHECKSUM, ACTIVE])
 * =s udp
 * creates packets flows with static UDP/IP6/Ethernet headers
 * =d
 * FastUDPFlowsIP6 is a benchmark tool. At initialization time, FastUDPFlowsIP6
 * creates FLOWS number of UDP/IP packets of length LENGTH (min 60), with source
 * ethernet address SRCETH, source IP address SRCIP, destination ethernet
 * address DSTETH, and destination IP address DSTIP. Source and
 * destination ports are randomly generated. The UDP checksum is calculated if
 * CHECKSUM is true; it is true by default. Each time the FastUDPFlowsIP6
 * element is called, it selects a flow, increments the reference count on the
 * skbuff created and returns the skbuff object w/o copying or cloning.
 * Therefore, the packet returned by FastUDPFlowsIP6 should not be modified.
 *
 * FastUDPFlowsIP6 sents packets at RATE packets per second. It will send LIMIT
 * number of packets in total. Each flow is limited to FLOWSIZE number of
 * packets. After FLOWSIZE number of packets are sent, the src and dst port
 * will be modified.
 *
 * After FastUDPFlowsIP6 has sent LIMIT packets, it will calculate the average
 * send rate (packets per second) between the first and last packets sent and
 * make that available in the rate handler.
 *
 * By default FastUDPFlowsIP6 is ACTIVE.
 *
 * =h count read-only
 * Returns the total number of packets that have been generated.
 * =h rate read/write
 * Returns or sets the RATE parameter.
 * =h reset write
 * Reset and restart.
 * =h active write
 * Change ACTIVE
 *
 * =e
 *  FastUDPFlowsIP6(100000, 500000, 60,
 *               0:0:0:0:0:0, 3ffe::1.0.0.1,
 *               1:1:1:1:1:1, 3ffe::1.0.0.1,
 *               100, 10)
 *    -> ToDevice;
 */

class FastUDPFlowsIP6 : public BatchElement {
    public:
        FastUDPFlowsIP6() CLICK_COLD;
        ~FastUDPFlowsIP6() CLICK_COLD;

        const char *class_name() const override  { return "FastUDPFlowsIP6"; }
        const char *port_count() const override  { return PORTS_0_1; }
        const char *processing() const override  { return AGNOSTIC; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;
        Packet *pull(int);

    #if HAVE_BATCH
        PacketBatch *pull_batch(int port, unsigned max);
    #endif


        bool run_task(Task*) override;
        void run_timer(Timer*) override;

        void cleanup_flows();
        static int length_write_handler (const String &s, Element *e, void *, ErrorHandler *errh);

        void add_handlers() CLICK_COLD;
        void reset();
        unsigned count() { return _count; }
        click_jiffies_t first() { return _first; }
        click_jiffies_t last() { return _last; }

        static const unsigned NO_LIMIT = 0xFFFFFFFFU;

        GapRate _rate;
        unsigned _count;
        unsigned _limit;
        bool _active;
        bool _stop;

    private:

        bool _rate_limited;
        unsigned _len;
        click_ether _ethh;
        IP6Address _sip6addr;
        IP6Address _dip6addr;
        unsigned int _nflows;
        unsigned int _flowsize;
        unsigned int _flowburst;
        bool _cksum;
        bool _sequential;
        click_jiffies_t _first;
        click_jiffies_t _last;
        struct state_t {
            state_t() : index(0), burst_count(1) {

            }
            unsigned index;
            unsigned burst_count;
        };
        per_thread<state_t> _last_flow;
        struct flow_t {
            Packet *packet;
            unsigned flow_count;
        };
        flow_t *_flows;
        Task _task;
        Timer _timer;

        void change_ports(int);
        Packet *get_packet();

        static int eth_write_handler(const String &, Element *, void *, ErrorHandler *);

        void set_length(unsigned len) {
            if (len < 60) {
                click_chatter("warning: packet length < 60, defaulting to 60");
                len = 60;
            }
            _len = len;
        }
};

CLICK_ENDDECLS
#endif
