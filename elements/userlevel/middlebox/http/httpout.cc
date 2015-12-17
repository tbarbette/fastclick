#include <click/config.h>
#include "httpout.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>

CLICK_DECLS

HTTPOut::HTTPOut()
{

}

int HTTPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* HTTPOut::processPacket(Packet* p)
{
    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(HTTPOut)
//ELEMENT_MT_SAFE(HTTPOut)
