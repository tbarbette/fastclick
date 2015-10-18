#ifndef CLICK_MIDDLEBOX_HH
#define CLICK_MIDDLEBOX_HH
#include <click/element.hh>
CLICK_DECLS

struct flow_maintainer
{
    int seq_offset_initiator;
    int seq_offset_other;
    unsigned long packets_seen;
};


class Middlebox : public Element
{
public:
    Middlebox() CLICK_COLD;

    const char *class_name() const        { return "Middlebox"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int, Packet *);
    Packet *pull(int);
    void helloWorld();
    struct flow_maintainer* getFlowMaintainer();
    bool isLinked();

protected:
    Packet* processPacket(Packet*);
    void processContent(WritablePacket*, unsigned char*, uint32_t);
    void setChecksum(WritablePacket*);

private:
    struct flow_maintainer* flow_maintainer;
    Middlebox* linkedMiddlebox;
    void increasePacketsSeen();
    void updateSeqNumbers(WritablePacket*, bool);
    void updatePayloadSize(WritablePacket*, uint32_t);
    bool header_processed;


};

CLICK_ENDDECLS
#endif
