/*
 * ipout.{cc,hh} -- exit point of an IP path in the stack of the middlebox
 * Romain Gaillard
 * Tom Barbette
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>
#include "ipout.hh"

CLICK_DECLS

IPOut::IPOut() : _readonly(false), _checksum(true)
{

}

int IPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if(Args(conf, this, errh)
        .read_p("READONLY", _readonly)
        .read("CHECKSUM", _checksum)
        .complete() < 0)
            return -1;

    return 0;
}

void IPOut::push_batch(int port, PacketBatch* flow)
{
    if (!_readonly) {
        EXECUTE_FOR_EACH_PACKET([this](Packet* p){
            WritablePacket *packet = p->uniqueify();

            // Recompute the IP checksum
            if (_checksum)
                computeIPChecksum(packet);

            return packet;
        }, flow);
    }
    output(0).push_batch(flow);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPOut)
ELEMENT_MT_SAFE(IPOut)
