#ifndef MIDDLEBOX_MIDSTAT_HH
#define MIDDLEBOX_MIDSTAT_HH
#include <click/element.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include "stackelement.hh"
#include <click/flowbuffer.hh>

CLICK_DECLS

/**
 * Structure used by the MidStat element
 */
struct fcb_MidStat
{
    int buffer;
};

struct mstat
{
    IPAddress addrs[2];
    uint16_t ports[2];
    int state;
    struct mstat* next;
};

/*
=c

MidStat([CLOSECONNECTION])

=s middlebox


 */


class MidStat : public StackSpaceElement<fcb_MidStat>
{
public:
    /** @brief Construct an MidStat element
     */
    MidStat() CLICK_COLD;

    const char *class_name() const        { return "MidStat"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    void push_batch(int port, fcb_MidStat* fcb, PacketBatch*) override;

protected:


    per_thread<MemoryPool<struct mstat>> poolStat;
    Vector<struct mstat*> stats;
};

CLICK_ENDDECLS
#endif
