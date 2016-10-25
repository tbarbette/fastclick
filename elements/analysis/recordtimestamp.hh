#ifndef CLICK_RECORDTIMESTAMP_HH
#define CLICK_RECORDTIMESTAMP_HH

#include <cassert>
#include <vector>

#include <click/element.hh>
#include <click/timestamp.hh>

CLICK_DECLS

/*
 * TODO: documentation.
 */
class RecordTimestamp : public Element {
public:
    RecordTimestamp() CLICK_COLD;
    ~RecordTimestamp() CLICK_COLD;

    const char *class_name() const { return "RecordTimestamp"; }
    const char *port_count() const { return PORTS_1_1; }
    const char *processing() const { return PUSH; }
    const char *flow_code() const { return "x/x"; }
    int configure_phase() const { return CONFIGURE_PHASE_DEFAULT; }
    bool can_live_reconfigure() const { return false; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int, Packet *);
    Timestamp get(uint64_t i);

private:
    uint64_t _count;
    std::vector<Timestamp> _timestamps;
};

inline Timestamp RecordTimestamp::get(uint64_t i) {
    assert(i < _timestamps.size());
    return _timestamps[i];
}

extern RecordTimestamp *recordtimestamp_singleton_instance;

inline RecordTimestamp *get_recordtimestamp_instance() {
    return recordtimestamp_singleton_instance;
}


CLICK_ENDDECLS

#endif
