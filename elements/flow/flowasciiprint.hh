#ifndef MIDDLEBOX_FlowASCIIPrint_HH
#define MIDDLEBOX_FlowASCIIPrint_HH
#include <click/element.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include "stackelement.hh"
#include <click/flowbuffer.hh>

CLICK_DECLS

/**
 * Structure used by the FlowASCIIPrint element
 */
struct fcb_FlowASCIIPrint
{
    FlowBuffer flowBuffer;

    fcb_FlowASCIIPrint()
    {
    }
};

class FlowASCIIPrint : public StackSpaceElement<fcb_FlowASCIIPrint>
{
public:
    /** @brief Construct an FlowASCIIPrint element
     */
    FlowASCIIPrint() CLICK_COLD;

    const char *class_name() const        { return "FlowASCIIPrint"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    void push_batch(int port, fcb_FlowASCIIPrint* fcb, PacketBatch*) override;

};

CLICK_ENDDECLS
#endif
