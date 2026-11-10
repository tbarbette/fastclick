#ifndef CLICK_RESETIPCHECKSUM_HH
#define CLICK_RESETIPCHECKSUM_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
CLICK_DECLS

/*
 * =c
 * ResetIPChecksum([L4])
 * =s ip
 * sets IP packets' checksums
 * =d
 * Expects an IP packet as input.
 *
 * Resets the IP checksum and asks hardware to fix it. If the packet is not a
 * DPDK buffer, or if Click is compiled without DPDK, the checksum is computed
 * in software instead.
 *
 * Keyword arguments are:
 *
 * =over 8
 *
 * =item L4
 *
 * Boolean. Also reset the TCP or UDP checksum. Defaults to false.
 *
 * =back
 *
 * =h drops read-only
 * Returns the number of dropped malformed packets.
 *
 * =a SetIPChecksum, SetTCPChecksum, SetUDPChecksum, CheckIPHeader, DecIPTTL,
 * ResetIPDSCP, IPRewriter */

class ResetIPChecksum : public BatchElement { public:

    ResetIPChecksum() CLICK_COLD;
    ~ResetIPChecksum() CLICK_COLD;

    const char *class_name() const override		{ return "ResetIPChecksum"; }
    const char *port_count() const override		{ return PORTS_1_1; }
    void add_handlers() override CLICK_COLD;

    int configure(Vector<String> &conf, ErrorHandler *errh) override CLICK_COLD;

    Packet *simple_action(Packet *p);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *);
#endif

  private:
    bool _l4;
    unsigned _drops;
    unsigned _warned;
};

CLICK_ENDDECLS
#endif
