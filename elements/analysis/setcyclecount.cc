/*
 * setcyclecount.{cc,hh} -- set cycle counter annotation
 * Eddie Kohler
 *
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
#include "setcyclecount.hh"
#include <click/glue.hh>
#include <click/packet_anno.hh>

SetCycleCount::SetCycleCount()
{
}

SetCycleCount::~SetCycleCount()
{
}

Packet*
SetCycleCount::simple_action(Packet *p)
{
  SET_PERFCTR_ANNO(p, click_get_cycles());
  return p;
}

ELEMENT_REQUIRES(int64)
EXPORT_ELEMENT(SetCycleCount)
ELEMENT_MT_SAFE(SetCycleCount)
