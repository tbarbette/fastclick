// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * truncate.{cc,hh} -- limits packet length
 * Eddie Kohler
 *
 * Copyright (c) 2004 Regents of the University of California
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
#include "truncatefcs.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/packet_anno.hh>
CLICK_DECLS

TruncateFCS::TruncateFCS()
{
}

int
TruncateFCS::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _extra_anno = true;
    return Args(conf, this, errh)
	.read("EXTRA_LENGTH", _extra_anno)
	.complete();
}

Packet *
TruncateFCS::simple_action(Packet *p)
{
	if (_extra_anno && EXTRA_LENGTH_ANNO(p) > 4)
	    SET_EXTRA_LENGTH_ANNO(p, EXTRA_LENGTH_ANNO(p) - 4);
    else
        p->take(4);
    return p;
}

void
TruncateFCS::add_handlers()
{
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TruncateFCS)
ELEMENT_MT_SAFE(TruncateFCS)
