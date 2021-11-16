#ifndef MIDDLEBOX_CTXASCIIPrint_HH
#define MIDDLEBOX_CTXASCIIPrint_HH
#include <click/element.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/flow/ctxelement.hh>
#include <click/flowbuffer.hh>

CLICK_DECLS

/**
 * Structure used by the CTXASCIIPrint element
 */
struct fcb_CTXASCIIPrint
{
    FlowBuffer flowBuffer;

    fcb_CTXASCIIPrint()
    {
    }
};

class CTXASCIIPrint : public CTXSpaceElement<fcb_CTXASCIIPrint>
{
public:
    /** @brief Construct an CTXASCIIPrint element
     */
    CTXASCIIPrint() CLICK_COLD;

    const char *class_name() const        { return "CTXASCIIPrint"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    void push_flow(int port, fcb_CTXASCIIPrint* fcb, PacketBatch*) override;

private:
    bool _active;

};

CLICK_ENDDECLS
#endif
