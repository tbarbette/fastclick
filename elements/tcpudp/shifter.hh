#ifndef CLICK_SHIFTER_HH
#define CLICK_SHIFTER_HH

#include <click/config.h>
#include <click/batchelement.hh>


CLICK_DECLS

/**
 * =c
 * Shifter(IP_OFFSET, PORT_OFFSET, I<keywords>)
 *
 * 
 * =s  nat
 * 
 * Shifts IP and ports.
 *
 * =d
 * Shifts IPs and ports by the given offset, modulo the size of the field.
 * Accepts TCP and UDP packets encapsulated in IPv4.
 * Other L4 packets will be emitted untouched.
 * 
 * Keywords:
 * =item IP_OFFSET
 * Shifting for the source IP
 * =item PORT_OFFSET
 * Shifting for the source port
 * =item IP_OFFSET_DST
 * Shifting for the destination IP
 * =item PORT_OFFSET_DST
 * Shifting for the destination port
 * 
 * =e
 * 
 * FromDump(trace.pcap)
 * -> Strip(14) -> CheckIPHeader(CHECKSUM false)
 * -> Shifter(1,1,1,1)
 * -> CheckIPHeader(CHECKSUM false) 
 * -> ToDump(trace_1.pcap)
 * 
 **/
class Shifter : public BatchElement {
  public:
    Shifter() CLICK_COLD;
    ~Shifter() CLICK_COLD;

    const char *class_name() const override { return "Shifter"; }
    const char *port_count() const override { return "1/1"; }
    const char *processing() const override { return PUSH; }
    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    inline Packet *simple_action(Packet *p) override;

#if HAVE_BATCH
    inline void push_batch(int, PacketBatch *batch) override;
#endif

  private:
    Packet* process(Packet *p);

    int _ipoffset;
    int _portoffset;
    int _ipoffset_dst;
    int _portoffset_dst;
};

CLICK_ENDDECLS
#endif
