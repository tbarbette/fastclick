#ifndef CLICK_FLOWHYPERSCAN_HH
#define CLICK_FLOWHYPERSCAN_HH
#include <click/config.h>
#include <click/flow/flowelement.hh>
extern "C" {
    #include <hs/hs.h>
}

CLICK_DECLS

/*
 * State of one stream
 */
struct FlowHyperScanState {
	FlowHyperScanState() {
	}
	hs_stream_t* stream;
    bool found;
};

/**
 * =title FlowHyperScan
 *
 * =c
 *
 * FlowHyperScan(PATTERNS)
 *
 * =s flow
 *
 * Flow-based IDS using the HyperScan library
 *
 * =d
 *
 * This element uses the HyperScan library to implement a pattern matcher that
 * is not subject to eviction by splitting the stream of attack at the right
 * place as it keeps a per-flow record of the DFA.
 *
 *
 */
class FlowHyperScan : public FlowSpaceElement<FlowHyperScanState> {
    public:
        FlowHyperScan() CLICK_COLD;
        ~FlowHyperScan() CLICK_COLD;

        const char *class_name() const override		{ return "FlowHyperScan"; }
        const char *port_count() const override		{ return "1/1"; }
        const char *processing() const override		{ return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *errh) override CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;

        void push_flow(int, FlowHyperScanState*, PacketBatch *);

        bool is_valid_patterns(Vector<String> &patterns, ErrorHandler *errh);

    protected:
        hs_database_t *db_streaming;
        bool _payload_only;
        unsigned _flags;
        bool _verbose;
        bool _kill;
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
