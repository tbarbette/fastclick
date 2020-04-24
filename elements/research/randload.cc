/*
 * RandLoad.{cc,hh}
 */

#include "randload.hh"

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>

CLICK_DECLS

RandLoad::RandLoad() {

};

RandLoad::~RandLoad() {

}

int
RandLoad::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .read_or_set("MIN", _min, 1)
               .read_or_set("MAX", _max, 100)
               .complete() < 0)
        return -1;

    return 0;
}


int RandLoad::initialize(ErrorHandler *errh) {
    return 0;
}

void RandLoad::push_batch(int port, PacketBatch* batch) {
	int r;
    auto fnt = [this,&r](Packet* p)  {
    	int w =  _min + ((*_gens)() / (UINT_MAX / (_max - _min) ));
        for (int i = 0; i < w - 1; i ++) {
            r = (*_gens)();
        }
        return p;
    };
    EXECUTE_FOR_EACH_PACKET(fnt, batch);

    output_push_batch(0, batch);
}


CLICK_ENDDECLS

EXPORT_ELEMENT(RandLoad)
ELEMENT_MT_SAFE(RandLoad)
