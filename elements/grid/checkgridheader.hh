#ifndef CHECKGRIDHEADER_HH
#define CHECKGRIDHEADER_HH
#include <click/batchelement.hh>
#include <click/atomic.hh>
#include <click/glue.hh>
CLICK_DECLS

/*
 * =c
 * CheckGridHeader([BADADDRS])
 * =s Grid
 * =d
 * Expects Grid packets as input.
 * Checks that the packet's length is reasonable,
 * and that the Grid header length, length, and
 * checksum fields are valid.
 *
 * =over 8
 *
 * =h count read-only
 *
 * Returns the number of correct packets CheckGridHeader has seen.
 *
 * =h drops read-only
 *
 * Returns the number of incorrect packets CheckGridHeader has seen.
 *
 * =back
 *
 * =a
 * SetGridChecksum
 */

class CheckGridHeader : public SimpleElement<CheckGridHeader> {
    public:
        CheckGridHeader() CLICK_COLD;
        ~CheckGridHeader() CLICK_COLD;

        const char *class_name() const override { return "CheckGridHeader"; }
        const char *port_count() const override { return "1/1-2"; }
        const char *processing() const override { return PROCESSING_A_AH; }

        void add_handlers() CLICK_COLD;

        Packet *simple_action(Packet *p);

    private:
        atomic_uint64_t _count;
        atomic_uint64_t _drops;

        enum { h_count, h_drops };

        void drop(Packet *p);
        static String read_handler(Element *e, void *thunk) CLICK_COLD;
};

CLICK_ENDDECLS
#endif
