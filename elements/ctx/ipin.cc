/*
 * ipin.{cc,hh} -- entry point of an IP path in the stack of the middlebox
 * Romain Gaillard
 * Tom Barbette
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>
#include "ipin.hh"

CLICK_DECLS

IPIn::IPIn()
{

}

int
IPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if(Args(conf, this, errh)
            .complete() < 0)
        return -1;

    return 0;
}

void
IPIn::push_batch(int port, PacketBatch* flow)
{
    EXECUTE_FOR_EACH_PACKET([this](Packet* packet){
        // Compute the offset of the IP payload
        const click_ip *iph = packet->ip_header();
        unsigned iph_len = iph->ip_hl << 2;
        uint16_t offset = (uint16_t)(packet->network_header() + iph_len - packet->data());
        packet->setContentOffset(offset);
        return packet;
    }, flow);
    output(0).push_batch(flow);
}

FlowNode* IPIn::get_table(int,Vector<FlowElement*> context) {
    //click_chatter("%p{element} get table", this);
    context.push_back(this);
	FlowNode* root = FlowElementVisitor::get_downward_table(this, 0, context, true);
	return root;
}

FlowType IPIn::getContext(int port) {
    return FLOW_IP;
}

FlowNode*
IPIn::resolveContext(FlowType t, Vector<FlowElement*> contextStack) {
    String prot;
    switch (t) {
        case FLOW_UDP:
            prot = "9/11!";
            break;
        case FLOW_TCP:
            prot = "9/06!";
            break;
        case FLOW_ICMP:
            prot = "9/01!";
            break;
        default:
            return FlowElement::resolveContext(t, contextStack);
    }
    return FlowClassificationTable::parse(this, prot).root;
}

/*
virtual void IPIn::removeBytes(WritablePacket* packet, uint32_t position,
        uint32_t length)
{
    CTXElement::removeBytes(packet, position, length);
}
*/
CLICK_ENDDECLS
EXPORT_ELEMENT(IPIn)
ELEMENT_MT_SAFE(IPIn)
