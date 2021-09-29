// -*- c-basic-offset: 4 -*-
/*
 * batchtest.{cc,hh} --
 * Tom Barbette
 *
 * Copyright (c) 2016 University of Liege
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
#include "batchtest.hh"

CLICK_DECLS

BatchTest::BatchTest()
{
}

void
BatchTest::push(int port,Packet* p)
{
    click_chatter("%p{element}: Packet push",this);
    output(port).push(p);
}

void
BatchTest::push_batch(int port,PacketBatch* batch)
{
    click_chatter("%p{element}: Batch push of %d packets",this,batch->count());
    assert(batch->count() == batch->first()->find_count());
    assert(batch->tail() == batch->first()->find_tail());
    output_push_batch(port, batch);
}

Packet*
BatchTest::pull(int port)
{
    click_chatter("%p{element}: Packet pull",this);
    return input(port).pull();
}

PacketBatch*
BatchTest::pull_batch(int port,unsigned max)
{
    PacketBatch* batch = input(port).pull_batch(max);
    if (batch)
        click_chatter("%p{element}: Batch pull of %d packets",this,batch->count());
    else
        click_chatter("%p{element}: Batch pull of 0 packets",this);
    return batch;
}



BatchElementTest::BatchElementTest()
{
}

void
BatchElementTest::push(int port,Packet* p)
{
    click_chatter("%p{element}: Packet push",this);
    output(port).push(p);
}

void
BatchElementTest::push_batch(int port,PacketBatch* batch)
{
    click_chatter("%p{element}: Batch push of %d packets",this, batch->count());
    Element::push_batch(port,batch);
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(batch)
EXPORT_ELEMENT(BatchTest)
EXPORT_ELEMENT(BatchElementTest)
