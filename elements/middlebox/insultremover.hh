#ifndef MIDDLEBOX_INSULTREM_HH
#define MIDDLEBOX_INSULTREM_HH
#include <click/element.hh>
#include "stackelement.hh"
#include "flowbuffer.hh"
#include "flowbufferentry.hh"

CLICK_DECLS

#define POOL_BUFFER_ENTRIES_SIZE 300

class InsultRemover : public StackElement
{
public:
    InsultRemover() CLICK_COLD;

    const char *class_name() const        { return "InsultRemover"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

protected:
    Packet* processPacket(struct fcb*, Packet*);
    int removeInsult(struct fcb* fcb, const char *insult);

    // Will be associated to the thread managing this direction of the flow as an InsultRemover
    // element is responsible for a direction of the flow and thus used by only one thread
    MemoryPool<struct flowBufferEntry> poolBufferEntries;
};

CLICK_ENDDECLS
#endif
