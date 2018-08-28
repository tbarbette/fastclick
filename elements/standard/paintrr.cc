/*
 * paintrr.{cc,hh} -- element routes packets to one output of several
 * Tom Barbette.  Based on PaintSwitch element
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
#include "paintrr.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/packet_anno.hh>
CLICK_DECLS

PaintRR::PaintRR()
{
}

int
PaintRR::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int anno = PAINT_ANNO_OFFSET;
    if (Args(conf, this, errh)
            .read_p("ANNO", AnnoArg(1), anno).complete() < 0)
	return -1;
    _anno = anno;
    return 0;
}

inline int
PaintRR::classify(Packet *p)
{
    return static_cast<int>(p->anno_u8(_anno)) % noutputs();
}

CLICK_ENDDECLS
EXPORT_ELEMENT(PaintRR)
ELEMENT_MT_SAFE(PaintRR)
