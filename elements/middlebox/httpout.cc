#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "httpout.hh"

CLICK_DECLS

HTTPOut::HTTPOut()
{

}

int HTTPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* HTTPOut::processPacket(struct fcb*, Packet* p)
{
    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(HTTPOut)
//ELEMENT_MT_SAFE(HTTPOut)
