/*
 * flowlock.{cc,hh} - Per-flow lock
 *
 * Copyright (c) 2019-2020 Tom Barbette, KTH Royal Institute of Technology
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
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <click/flow/flow.hh>
#include "flowlock.hh"

CLICK_DECLS

FlowLock::FlowLock()
{
}

FlowLock::~FlowLock()
{
}

int
FlowLock::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
       .complete() < 0)
        return -1;
    return 0;
}

int FlowLock::initialize(ErrorHandler *errh)
{
    return 0;
}

void FlowLock::push_flow(int port, FlowLockState* flowdata, PacketBatch* batch)
{
    flowdata->lock.acquire();
    output_push_batch(0, batch);
    flowdata->lock.release();
}

CLICK_ENDDECLS

EXPORT_ELEMENT(FlowLock)
ELEMENT_MT_SAFE(FlowLock)
