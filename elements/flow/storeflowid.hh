#ifndef CLICK_STOREFLOWID_HH
#define CLICK_STOREFLOWID_HH
#include <click/config.h>
#include <click/glue.hh>
#include <click/multithread.hh>
#include <click/flow/flowelement.hh>

#define STOREFLOWID_FLOW_TIMEOUT 1000 * 1000

CLICK_DECLS


/**
 * =c
 * StoreFlowID(OFFSET)
 * 
 *
 * =s flow
 *
 * Store a flow identifier in each packet
 *
 * =d
 *
 * Store a 32-bit flow identifier at OFFSET bytes in the packet.
 * Each new flow will have a different flow identifier, 
 * each core (if multiple threads will visit the element)
 * will have a different identifier space.
 *
 * Needs a FlowIPManager in front.
 *
 *
 * Keywords:
 * =item OFFSET
 * The flowid offset. Default is 40.
 * =item CAPACITY
 * Capacity of the flow-table that handles the identifiers. Default is 65536
 * =item VERBOSE
 * Be verbose and print messages. Default is 0
 *
 *
 * =e 
 *
 *  FromMinDump(trace.pcap)
 *  -> CheckIPHeader(CHECKSUM false)
 *  -> FlowIPManagerIMP()
 *  -> StoreFlowID(OFFSET 40)
 *  -> ConsistencyCheck(OFFSET 40)
 *  -> Discard
 *
 *   DriverManager(wait,  read cc.broken_ratio)
 *
 *
 * =h broken_connections read
 * Total number of broken connections
 *
 * =h broken_packets read
 * Total number of broken packets
 *
 * =h broken_connections_ratio read
 * Ratio between total broken connections and total connections seen
 *
 * =h broken_packets_ratio read
 * Ratio between total broken packetsand total packets seen
 *
 *
 **/
struct SFEntry {
    uint64_t flowid;
    SFEntry (uint64_t fid) : flowid(fid) {
    }
};


class StoreFlowID : public FlowStateElement<StoreFlowID, SFEntry> {
    public:
        StoreFlowID() CLICK_COLD {};
        ~StoreFlowID() CLICK_COLD {};

        const char *class_name() const override { return "StoreFlowID"; }
        const char *port_count() const override { return "1/1"; }
        const char *processing() const override { return PUSH; }
        static const int timeout = STOREFLOWID_FLOW_TIMEOUT;

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *errh) override CLICK_COLD;

        bool new_flow(SFEntry*, Packet*);
        void release_flow(SFEntry*) {};

        void push_flow(int,SFEntry*, PacketBatch *);

private:


	uint64_t * _flowids;
	uint16_t _offset;
	bool _random;


};


CLICK_ENDDECLS
#endif
