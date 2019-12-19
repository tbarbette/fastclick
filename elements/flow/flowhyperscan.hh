#ifndef CLICK_FlowHyperScan_HH
#define CLICK_FlowHyperScan_HH
#include <click/config.h>
#include <click/flow/flowelement.hh>
extern "C" {
    #include <hs/hs.h>
}

CLICK_DECLS

struct FlowHyperScanState {
	FlowHyperScanState() {

	}
	hs_stream_t* stream;
};

class FlowHyperScan : public FlowSpaceElement<FlowHyperScanState> {

public:

    FlowHyperScan() CLICK_COLD;
    ~FlowHyperScan() CLICK_COLD;

    const char *class_name() const		{ return "FlowHyperScan"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *errh) override CLICK_COLD;

    void push_batch(int, FlowHyperScanState*, PacketBatch *);

    bool is_valid_patterns(Vector<String> &patterns, ErrorHandler *errh);

protected:
    hs_database_t *db_streaming;
    bool _payload_only;
    unsigned _flags;
    bool _verbose;
    struct FlowHyperScanThreadState {
        FlowHyperScanThreadState() : scratch(0), matches(0) {

        }
        hs_scratch* scratch;
        unsigned matches;
    };
    per_thread<FlowHyperScanThreadState> _state;
    hs_scratch* _scratch;
};





CLICK_ENDDECLS
#endif
