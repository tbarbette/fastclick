/*
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
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
#include "cpuswitch.hh"
#include <click/error.hh>

CLICK_DECLS

CPUSwitch::CPUSwitch()
{
}

CPUSwitch::~CPUSwitch()
{
}

bool
CPUSwitch::get_spawning_threads(Bitvector& b, bool isoutput, int port) {
    if (port == -1 || !isoutput)
        return true;

    Bitvector m = get_pushing_threads();
    
    for (unsigned i = 0; i < (unsigned)b.size(); i++) {
	if (!m[i])
		continue;
        if ((unsigned)port == (i % noutputs())) {
            b[i] = true;
        }
    }
    return false;
}

void
CPUSwitch::push(int, Packet *p)
{
  int n = click_current_cpu_id() % noutputs();
  output(n).push(p);
}

#if HAVE_BATCH
void
CPUSwitch::push_batch(int, PacketBatch *batch)
{
  int n = click_current_cpu_id() % noutputs();
  output(n).push_batch(batch);
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(CPUSwitch)
ELEMENT_MT_SAFE(CPUSwitch)
