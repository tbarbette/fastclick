#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "testcrash.hh"

CLICK_DECLS

TestCrash::TestCrash()
{
}

TestCrash::~TestCrash()
{

}

int TestCrash::configure(Vector<String> &, ErrorHandler *)
{
    return 0;
}

void TestCrash::push(int, Packet *p)
{
    WritablePacket *packet = p->uniqueify();

    click_chatter("Packet!");


    packet = packet->put(1500);
    packet->take(1500);

    output(0).push(packet);
}

CLICK_ENDDECLS

EXPORT_ELEMENT(TestCrash)
