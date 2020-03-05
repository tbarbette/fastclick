#ifndef CLICK_FLOWLOCK_HH
#define CLICK_FLOWLOCK_HH
#include <click/config.h>
#include <click/multithread.hh>
#include <click/hashtablemp.hh>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/flow/flowelement.hh>
#include <random>

CLICK_DECLS

struct FlowLockState {
    FlowLockState() : lock() {}
    Spinlock lock;
};

/**
 * =s
 *
 * FlowLock()
 *
 * =c flow
 *
 * Flock the downards path per-flow, so packets of the same flow cannot go further at the same time.
 */
class FlowLock : public FlowSpaceElement<FlowLockState> {
    public:
        FlowLock() CLICK_COLD;
        ~FlowLock() CLICK_COLD;

        const char *class_name() const { return "FlowLock"; }
        const char *port_count() const { return "1/1"; }
        const char *processing() const { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *errh) override CLICK_COLD;

        void push_batch(int, FlowLockState*, PacketBatch *);
};

CLICK_ENDDECLS
#endif
