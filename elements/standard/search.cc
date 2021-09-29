// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * Search.{cc,hh} -- element strips TCP header from front of packet
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
#include <clicknet/tcp.h>
#include "search.hh"
CLICK_DECLS

Search::Search()
{
}

int
Search::configure(Vector<String> &conf, ErrorHandler *errh)
{
	int anno = PAINT2_ANNO_OFFSET;
	String pattern;
	bool strip_after = true;
	bool set_anno = true;
    if (Args(conf, this, errh)
    		.read_mp("PATTERN", pattern)
			.read("STRIP_AFTER", strip_after)
			.read("ANNO", AnnoArg(2), anno)
			.read("SET_ANNO", _set_anno)
			.complete() < 0)
				return -1;

    if (pattern.length() == 0) {
	    return errh->error("Cannot search for an empty string!");
    }
    _anno = anno;
    _set_anno = set_anno;
    _pattern = pattern;
    _strip_after = strip_after;

    return 0;
}


int
Search::action(Packet* p) {
	const char* f = String::make_stable((const char*)p->data(), p->length()).search(_pattern);
	if (f == 0) {
		return 1;
	}

	unsigned n = (f - (const char*)p->data());
	if (_strip_after)
		n += _pattern.length();
	p->pull(n);
	if (_set_anno)
		p->set_anno_u16(_anno, p->anno_u16(_anno) + n);
    return 0;
}

void
Search::push(int, Packet *p) {

    int o = action(p);
	output(o).push(p);
}

#if HAVE_BATCH
void
Search::push_batch(int port, PacketBatch *batch) {
    CLASSIFY_EACH_PACKET(2, action, batch, checked_output_push_batch);
}
#endif

CLICK_ENDDECLS

EXPORT_ELEMENT(Search)
ELEMENT_MT_SAFE(Search)
