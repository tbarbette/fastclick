#ifndef CLICK_CONSISTENCYCHECK_HH
#define CLICK_CONSISTENCYCHECK_HH
#include <click/batchelement.hh>
#include <click/string.hh>
#include <click/dpdk_glue.hh>
#include <click/hashtablemp.hh>
#include <click/multithread.hh>
CLICK_DECLS

/**
 * =c
 * ConsistencyCheck(OFFSET)
 * 
 *
 * =s flow
 *
 * Check flow consistency
 *
 * =d
 *
 * Check that a given 4-byte value at OFFSET has always the same value.
 * E.g. check that a flow classifier always assign the same flow-id to
 * any 5-tuple.
 * 
 * This can be used to debug and verify the functionalities of the 
 * FlowIPManager elements.
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





class ConsistencyCheck : public BatchElement { public:

    ConsistencyCheck() CLICK_COLD;

    const char *class_name() const override	{ return "ConsistencyCheck"; }
    const char *port_count() const override	{ return PORTS_1_1; }
    int configure_phase() const override        { return CONFIGURE_PHASE_PRIVILEGED + 1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    int initialize(ErrorHandler *errh) override CLICK_COLD;

#ifdef HAVE_BATCH
    void push_batch(int, PacketBatch * batch) override;
#endif

    static String read_handler(Element* e, void* thunk);
 private:

    void add_handlers() override CLICK_COLD;
    uint8_t _offset;
    uint8_t _verbose;
    int _table_size; 
    int _threads;
    bool _reverse;


    struct pcctable{
	pcctable() : hash(0), flowids(0) {
	}
	HashTableH<IPFlow5ID, int> * hash;
	atomic_uint64_t current;
	uint64_t * flowids;
	uint32_t * counts;
	uint32_t * broken;
	uint32_t too_short;
    } CLICK_ALIGNED(CLICK_CACHE_LINE_SIZE);

    pcctable * _tables;

    Packet * process(Packet *p);

};

CLICK_ENDDECLS
#endif
