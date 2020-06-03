/*
 * exactcpuswitch.{cc,hh} -- Element selects per-cpu path
 *
 * Tom Barbette
 *
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

void
ExactCPUSwitch::update_map() {
    Bitvector b = get_pushing_threads();
    map.resize(b.size());
    int j = 0;
    for (unsigned i = 0; i < b.size(); i++) {
        if (b[i] == false) {
            map[i] = -1;
            continue;
        }
        map[i] = j % noutputs();
        ++j;
    }
}

int
ExactCPUSwitch::initialize(ErrorHandler* errh) {
    update_map();
    return 0;
}


int
ExactCPUSwitch::thread_configure(ThreadReconfigurationStage, ErrorHandler* errh, Bitvector threads) {
    update_map();
    return 0;
}

bool
ExactCPUSwitch::get_spawning_threads(Bitvector& b, bool isoutput, int port) {
    if (map.size() == 0)
        update_map();
    if (port == -1 || !isoutput)
        return true;

    for (unsigned i = 0; i < (unsigned)map.size(); i++) {
        if ((unsigned)port % noutputs() == map[i]) {
            b[i] = true;
        }
    }
    return false;
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
