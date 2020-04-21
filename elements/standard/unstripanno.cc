/*
 * UnstripAnno.{cc,hh} -- element strips a dynamic amount of bytes from front of packet
 * Tom Barbette
 *
 * Copyright (c) 2020 KTH Royal Institute of Technology
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
#include <click/error.hh>
#include <click/glue.hh>
#include <click/packet_anno.hh>
#include "unstripanno.hh"
CLICK_DECLS

UnstripAnno::UnstripAnno()
{
}

int
UnstripAnno::configure(Vector<String> &conf, ErrorHandler *errh)
{
	int anno = PAINT2_ANNO_OFFSET;
    if (Args(conf, this, errh)
			.read_p("ANNO", AnnoArg(2), anno)
			.complete() < 0)
				return -1;
    _anno = anno;
    return 0;
}

Packet *
UnstripAnno::simple_action(Packet *p)
{
  return p->push(p->anno_u16(_anno));
}

CLICK_ENDDECLS
EXPORT_ELEMENT(UnstripAnno)
ELEMENT_MT_SAFE(UnstripAnno)
