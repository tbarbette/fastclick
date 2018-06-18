/*
 * ensureheadroom.{cc,hh} --
 * Tom Barbett
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
#include "ensureheadroom.hh"
#include <click/args.hh>

CLICK_DECLS

EnsureHeadroom::EnsureHeadroom()
    :  _headroom(Packet::default_headroom), _force(false)
{

}

int
EnsureHeadroom::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_p("HEADROOM", _headroom)
	.read_p("FORCE_SHRINK", _force)
	.complete() < 0)
	return -1;
    return 0;
}

inline Packet* EnsureHeadroom::smaction(Packet* p) {
	int length = p->length();
	Packet* q;
	if (p->headroom() < _headroom || _force) {
		q = p->shift_data(_headroom - p->headroom());
	}	else
		q = p;
	assert(length = q->length());
	return q;
}

#if HAVE_BATCH
PacketBatch*
EnsureHeadroom::simple_action_batch(PacketBatch *head)
{
    EXECUTE_FOR_EACH_PACKET(smaction,head);
	return head;
}
#endif
Packet*
EnsureHeadroom::simple_action(Packet *p)
{
    return smaction(p);
}


CLICK_ENDDECLS
EXPORT_ELEMENT(EnsureHeadroom)
ELEMENT_MT_SAFE(EnsureHeadroom)
