#ifndef CLICK_TIMESTAMPDIFF_HH
#define CLICK_TIMESTAMPDIFF_HH

#include <vector>

#include <click/element.hh>

CLICK_DECLS

/*
 * TODO: documentation.
 */
class TimestampDiff : public Element {
public:
    TimestampDiff() CLICK_COLD;
    ~TimestampDiff() CLICK_COLD;

    const char *class_name() const { return "TimestampDiff"; }
    const char *port_count() const { return PORTS_1_1; }
    const char *processing() const { return PUSH; }
    const char *flow_code() const { return "x/x"; }
    int configure_phase() const { return CONFIGURE_PHASE_DEFAULT; }
    bool can_live_reconfigure() const { return false; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;
    static String read_handler(Element*, void*) CLICK_COLD;

    void push(int, Packet *);

private:
    std::vector<unsigned> _delays;
};

CLICK_ENDDECLS

#endif
