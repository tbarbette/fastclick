#ifndef CLICK_FLOWCLASSIFIER_HH
#define CLICK_FLOWCLASSIFIER_HH
#include <click/batchelement.hh>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/flow.hh>
#include <click/multithread.hh>
#include <vector>


CLICK_DECLS

typedef struct {
    PacketBatch* batch;
    FlowControlBlock* fcb;
} FlowBatch;

typedef struct FlowCache_t{
    uint32_t agg;
    FlowControlBlock* fcb;
} FlowCache;

class FlowClassifier: public BatchElement {
    FlowClassificationTable _table;
    per_thread<FlowCache*> _cache;
    int _cache_size;
    int _cache_ring_size;
    int _cache_mask;
    bool _aggcache;
    int _pull_burst;
    bool _verbose;
    int _builder;
    int _builder_mask;
    bool _collision_is_life;
    int cache_hit;
    int cache_sharing;
    int cache_miss;

    per_thread<FlowBatch*> _builder_batch;
public:

    FlowClassifier() CLICK_COLD;

	~FlowClassifier() CLICK_COLD;

    const char *class_name() const		{ return "FlowClassifier"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return DOUBLE; }
    const char *flow_code() const		{ return "x/x"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;
    void cleanup(CleanupStage stage);

    FlowControlBlock* get_cache_fcb(Packet* p, uint32_t agg);
    void push_batch_simple(int port, PacketBatch*);
    void push_batch_builder(int port, PacketBatch*);
    void push_batch(int port, PacketBatch*);


public:
	FlowClassificationTable& table() {
		return _table;
	}

};
CLICK_ENDDECLS
#endif
