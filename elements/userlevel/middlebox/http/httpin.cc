#include <click/config.h>
#include "httpin.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>

CLICK_DECLS

HTTPIn::HTTPIn()
{

}

int HTTPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* HTTPIn::processPacket(Packet* p)
{
    // Compute the offset of the HTML payload
    const char* source = strstr((const char*)getPacketContentConst(p), "\r\n\r\n");
    if(source != NULL)
    {
        uint32_t offset = (int)(source - (char*)p->data() + 4);
        setContentOffset(p, offset);
    }

    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(HTTPIn)
//ELEMENT_MT_SAFE(HTTPIn)
