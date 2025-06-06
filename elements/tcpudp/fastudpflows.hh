#ifndef FASTUDPFLOWS_HH
#define FASTUDPFLOWS_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
#include <click/gaprate.hh>
#include <click/packet.hh>
#include <clicknet/ether.h>
#include <clicknet/udp.h>

CLICK_DECLS

/*
 * =c
 * FastUDPFlows(RATE, LIMIT, LEN,
 *              SRCETH, SRCIP,
 *              DSTETH, DSTIP,
 *              FLOWS, FLOWSIZE [, CHECKSUM, ACTIVE,
 *              SEQUENTIAL, DUPLICATE, STOP])
 * =s udp
 * creates packets flows with static UDP/IP/Ethernet headers
 * =d
 * FastUDPFlows is a benchmark tool. At initialization time, FastUDPFlows
 * creates FLOWS number of UDP/IP packets of length LENGTH (min 60), with source
 * ethernet address SRCETH, source IP address SRCIP, destination ethernet
 * address DSTETH, and destination IP address DSTIP. Source and
 * destination ports are randomly generated. The UDP checksum is calculated if
 * CHECKSUM is true; it is true by default. Each time the FastUDPFlows
 * element is called, it selects a flow, increments the reference count on the
 * skbuff created and returns the skbuff object w/o copying or cloning.
 * Therefore, the packet returned by FastUDPFlows should not be modified.
 *
 * FastUDPFlows sents packets at RATE packets per second. It will send LIMIT
 * number of packets in total. Each flow is limited to FLOWSIZE number of
 * packets. After FLOWSIZE number of packets are sent, the src and dst port
 * will be modified.
 *
 * After FastUDPFlows has sent LIMIT packets, it will calculate the average
 * send rate (packets per second) between the first and last packets sent and
 * make that available in the rate handler.
 *
 * By default FastUDPFlows is ACTIVE.
 *
 * Keyword arguments are:
 *
 * =over 8
 *
 * =item SEQUENTIAL. instead of generating random port numbers, generate sequential
 * one. Surprisingly this has a huge positive impact on performance.
 *
 * =item DUPLICATE. Duplicate packets instead of doing a shadow clone. If you know
 * packets will be modified, you might as well avoid the extra steps.
 *
 * =back
 *
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
 *  FastUDPFlows(100000, 500000, 60,
 *               0:0:0:0:0:0, 1.0.0.1,
 *               1:1:1:1:1:1, 2.0.0.2,
 *               100, 10)
 *    -> ToDevice;
 */

class FastUDPFlows : public BatchElement {
    public:
        FastUDPFlows() CLICK_COLD;
        ~FastUDPFlows() CLICK_COLD;

        const char *class_name() const override  { return "FastUDPFlows"; }
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
        struct in_addr _sipaddr;
        struct in_addr _dipaddr;
        unsigned int _nflows;
        unsigned int _flowsize;
        unsigned int _flowburst;
        unsigned int _burst;
        bool _cksum;
        bool _sequential;
        bool _duplicate;
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
        Packet *gen_packet();
        Packet *get_p();

        static int active_write_handler(const String &s, Element *e, void *, ErrorHandler *errh);
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
