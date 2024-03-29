#ifndef FASTTCPFLOWS_HH
#define FASTTCPFLOWS_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
#include <click/gaprate.hh>
#include <click/packet.hh>
#include <clicknet/ether.h>
#include <clicknet/tcp.h>

CLICK_DECLS

/*
 * =c
 * FastTCPFlows(RATE, LIMIT, LENGTH,
 *              SRCETH, SRCIP,
 *              DSTETH, DSTIP,
 *              FLOWS, FLOWSIZE [, ACTIVE])
 * =s tcp
 * creates packets flows with static TCP/IP/Ethernet headers
 * =d
 * FastTCPFlows is a benchmark tool. At initialization time, FastTCPFlows
 * creates FLOWS number of fake TCP/IP packets of length LENGTH (min 60), with
 * source ethernet address SRCETH, source IP address SRCIP, destination
 * ethernet address DSTETH, and destination IP address DSTIP. Source and
 * destination ports are randomly generated. The TCP checksum is calculated.
 * Each time the FastTCPFlows element is called, it selects a flow, increments
 * the reference count on the skbuff created and returns the skbuff object w/o
 * copying or cloning. Therefore, the packet returned by FastTCPFlows should
 * not be modified.
 *
 * FastTCPFlows sents packets at RATE packets per second. It will send LIMIT
 * number of packets in total. Each flow is limited to FLOWSIZE number of
 * packets. After FLOWSIZE number of packets are sent, the sort and dst port
 * will be modified. FLOWSIZE must be greater than or equal to 3. For each
 * flow, a SYN packet, a DATA packet, and a FIN packet are sent. These packets
 * have the invalid sequence numbers, in order to avoid recomputing checksum.
 *
 * After FastTCPFlows has sent LIMIT packets, it will calculate the average
 * send rate (packets per second) between the first and last packets sent and
 * make that available in the rate handler.
 *
 * By default FastTCPFlows is ACTIVE.
 *
 * Keyword arguments are:
 *
 * =over 8
 *
 * =item STOP
 *
 * Boolean. Stops the driver after generating LIMIT packets.
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
 *  FastTCPFlows(100000, 500000, 60,
 *               0:0:0:0:0:0, 1.0.0.1,
 *               1:1:1:1:1:1, 2.0.0.2,
 *               100, 10)
 *    -> ToDevice;
 */
class FastTCPFlows : public BatchElement {
    public:
        FastTCPFlows() CLICK_COLD;
        ~FastTCPFlows() CLICK_COLD;

        const char *class_name() const override    { return "FastTCPFlows"; }
        const char *port_count() const override    { return PORTS_0_1; }
        const char *processing() const override    { return PULL; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;
        Packet *pull(int);

    #if HAVE_BATCH
        PacketBatch *pull_batch(int, unsigned);
    #endif

        void add_handlers() CLICK_COLD;
        void reset();
        unsigned count() { return _count; }
        click_jiffies_t first() { return _first; }
        click_jiffies_t last() { return _last; }

        static const unsigned NO_LIMIT = 0xFFFFFFFFU;

        GapRate _rate;
        unsigned _count;
        unsigned _limit;
        bool _sequence;
        bool _active;
        bool _stop;

    private:
        bool _rate_limited;
        bool _sent_all_fins;
        unsigned _len;
        click_ether _ethh;
        struct in_addr _sipaddr;
        struct in_addr _dipaddr;
        unsigned int _nflows;
        unsigned int _last_flow;
        unsigned int _flowsize;
        bool _cksum;
        click_jiffies_t _first;
        click_jiffies_t _last;

        struct flow_t {
            Packet *syn_packet;
            Packet *fin_packet;
            Packet *data_packet;
            unsigned flow_count;
        };

        flow_t *_flows;
        void change_ports(int);
        Packet *get_packet();

};

CLICK_ENDDECLS
#endif
