/*
 * Paint2.{cc,hh} -- element sets packets' Paint2 annotation
 * Eddie Kohler, Robert Morris
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2008 Meraki, Inc.
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
#include "paint2.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/packet_anno.hh>
CLICK_DECLS

Paint2::Paint2()
{
}

int
Paint2::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int anno = PAINT2_ANNO_OFFSET;
    if (Args(conf, this, errh)
	.read_mp("COLOR", _color)
	.read_p("ANNO", AnnoArg(2), anno).complete() < 0)
	return -1;
    _anno = anno;
    return 0;
}

Packet *
Paint2::simple_action(Packet *p)
{
    p->set_anno_u16(_anno, _color);
    return p;
}

void
Paint2::add_handlers()
{
    add_data_handlers("color", Handler::OP_READ | Handler::OP_WRITE, &_color);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(Paint2)
ELEMENT_MT_SAFE(Paint2)
