/*
 * spinlockpush.{cc,hh} -- element acquires spinlock
 * Benjie Chen, Eddie Kohler, Tom Barbette
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology.
 * Copyright (c) 2008 Meraki, Inc.
 * Copyright (c) 1999-2013 Eddie Kohler
 * Copyright (c) 2015-2016 University of Liege
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
#include <click/router.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/nameinfo.hh>
#include "pathspinlock.hh"
CLICK_DECLS

int
PathSpinlock::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String name;
    if (Args(conf, this, errh)
            .read_p("LOCK", name)
            .complete() < 0)
        return -1;
    if (!name) {
        _lock = new Spinlock();
        _lock_release = true;
    } else {
        _lock_release = false;
        if (!NameInfo::query(NameInfo::T_SPINLOCK, this, name, &_lock, sizeof(Spinlock *)))
            return errh->error("cannot locate spinlock %s", name.c_str());
    }
    return 0;
}


void PathSpinlock::push(int i,Packet *p)	{
	_lock->acquire();
	output(i).push(p);
	_lock->release();
}

Packet* PathSpinlock::pull(int i)	{
	_lock->acquire();
	Packet* p = input(i).pull();
	_lock->release();
	return p;
}

#if HAVE_BATCH
void PathSpinlock::push_batch(int i,PacketBatch *batch)	{
	_lock->acquire();
	output(i).push_batch(batch);
	_lock->release();
}
PacketBatch* PathSpinlock::pull_batch(int i,unsigned max)	{
	_lock->acquire();
	PacketBatch* p = input(i).pull_batch(max);
	_lock->release(); 
	return p;
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(PathSpinlock)
