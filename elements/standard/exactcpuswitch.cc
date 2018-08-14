/*
 * Copyright (c) 2018 KTH Institute of Technology
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
#include "exactcpuswitch.hh"
#include <click/error.hh>

CLICK_DECLS

ExactCPUSwitch::ExactCPUSwitch() : map()
{
}

ExactCPUSwitch::~ExactCPUSwitch()
{
}

int
ExactCPUSwitch::initialize(ErrorHandler* errh) {
    Bitvector b = get_passing_threads();
    if (b.weight() == 0)
        return errh->error("No threads passing by this element...?");
    map.resize(b.weight());
    int j = 0;
    for (int i = 0; i < b.weight(); i++) {
        if (b[i] == false) continue;
        map[j] = i % noutputs();
        ++j;
    }
    return 0;
}


int
ExactCPUSwitch::thread_configure(ThreadReconfigurationStage, ErrorHandler* errh) {
    return 0;
}

void
ExactCPUSwitch::push(int, Packet *p)
{
  int n = map[click_current_cpu_id()];
  output(n).push(p);
}

#if HAVE_BATCH
void
ExactCPUSwitch::push_batch(int, PacketBatch *batch)
{
  int n = map[click_current_cpu_id()];
  output(n).push_batch(batch);
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(ExactCPUSwitch)
ELEMENT_MT_SAFE(ExactCPUSwitch)
