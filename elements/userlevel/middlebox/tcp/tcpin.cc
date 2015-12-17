#include <click/config.h>
#include "tcpin.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>

CLICK_DECLS

TCPIn::TCPIn()
{

}

int TCPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* TCPIn::processPacket(Packet* p)
{
    const click_tcp *tcph = p->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;
    int offset = (int)(p->transport_header() + tcph_len - p->data());
    setContentOffset(p, offset);

    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPIn)
//ELEMENT_MT_SAFE(TCPIn)
