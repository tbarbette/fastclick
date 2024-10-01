/*
 * storeflowid.{cc,hh} -- Store a flowid in each packet
 * Massimo Girondi
 *
 * Copyright (c) 2021 KTH Royal Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/flow/flow.hh>

#include "storeflowid.hh"


CLICK_DECLS

int
StoreFlowID::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(this, errh).bind(conf)
               .read_mp("OFFSET", _offset)
	       .read_or_set("RANDOM", _random, false)
               .consume() < 0)
		return -1;

    
    return 0;
}

int StoreFlowID::initialize(ErrorHandler *errh)
{
    auto passing = get_passing_threads();
    _flowids = CLICK_ALIGNED_NEW(uint64_t, passing.size());
    if(!_flowids)
	return errh->error("Error while allocating memory in StoreFlowID");
    memset(_flowids, 0, sizeof(uint64_t) * passing.size());
    int j = 0;
    for(int i=0; i<passing.size(); i++)
	if(passing[i])
	{
	    _flowids[i] = ( ((uint64_t)-1) / passing.weight()) * (j++);
	    click_chatter("[%i] initialized to %04X", _flowids[i]);
	}

    return 0;
}


bool StoreFlowID::new_flow(SFEntry* flowdata, Packet* p)
{

    uint64_t this_flowid = ++_flowids[click_current_cpu_id()];
    flowdata->flowid = this_flowid;

    return true;
}


void StoreFlowID::push_flow(int, SFEntry* flowdata, PacketBatch* batch)
{
    auto fnt = [this,flowdata](Packet*&p) -> bool {
        WritablePacket* q =p->uniqueify();
        p = q;
	
        if(p->length() >= _offset + sizeof(uint64_t)) {
            if(unlikely(_random))
            *((uint64_t *) &(q->data()[_offset])) = rand() + rand();
            else
            *((uint64_t *) &(q->data()[_offset])) = flowdata->flowid;
        }
        return true;
    };
    EXECUTE_FOR_EACH_PACKET_UNTIL_DROP(fnt, batch);

    if (batch)
        checked_output_push_batch(0, batch);
}




CLICK_ENDDECLS
EXPORT_ELEMENT(StoreFlowID)
ELEMENT_MT_SAFE(StoreFlowID)
