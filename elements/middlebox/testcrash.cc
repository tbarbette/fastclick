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

    for(int i = 0; i < 10; ++i)
        WritablePacket* newPacket = Packet::make(1500);

    output(0).push(packet);
}

CLICK_ENDDECLS

EXPORT_ELEMENT(TestCrash)
