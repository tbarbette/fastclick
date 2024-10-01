/*
 * ExactPaintSwitch.{cc,hh} -- Element selects per-cpu path
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
#include <click/args.hh>
#include "exactpaintswitch.hh"
#include <click/error.hh>

CLICK_DECLS

ExactPaintSwitch::ExactPaintSwitch() : map()
{
}

ExactPaintSwitch::~ExactPaintSwitch()
{
}

int
ExactPaintSwitch::configure(Vector<String> &conf, ErrorHandler *errh) {
    Vector<int> list;
    int anno = PAINT_ANNO_OFFSET;
    int def=-1;

    if (Args(conf, this, errh)
        .read_p("ANNO", AnnoArg(1), anno)
        .read("DEFAULT", def)
	    .read_all("MAP", list)
        .complete() != 0)
	return -1;

    if (def >= noutputs()) {
        return errh->error("Default port cannot be higher than the number of ports!");
    }

    _anno = anno;

    map.resize(256, (unsigned)def);
    for (int i = 0; i < list.size(); i++) {
        if (list[i] >= 256)
            return errh->error("Invalid Paint offset %d", list[i]);
        map[list[i]] = i;
    }

    return 0;

}

int
ExactPaintSwitch::initialize(ErrorHandler* errh) {
    return 0;
}

int
ExactPaintSwitch::classify(Packet *p)
{
  int o = static_cast<int>(p->anno_u8(_anno));
  int n = map.unchecked_at(o);
  return n;
}


CLICK_ENDDECLS
EXPORT_ELEMENT(ExactPaintSwitch)
ELEMENT_MT_SAFE(ExactPaintSwitch)
