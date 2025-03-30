/*
 * Duplicate.{cc,hh} -- element duplicates packets
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
#include "duplicate.hh"
#include <click/args.hh>
#include <click/error.hh>
CLICK_DECLS

Duplicate::Duplicate()
{
}

int
Duplicate::configure(Vector<String> &conf, ErrorHandler *errh)
{
    unsigned n = noutputs();
    if (Args(conf, this, errh).read_p("N", n).read_or_set("DATA_ONLY",_data_only,true).complete() < 0)
	return -1;
    if (n != (unsigned) noutputs())
	return errh->error("%d outputs implies %d arms", noutputs(), noutputs());
    return 0;
}

#if HAVE_BATCH
void
Duplicate::push_batch(int, PacketBatch *batch)
{
  int n = noutputs();
  for (int i = 0; i < n - 1; i++) {
    PacketBatch *q = batch->clone_batch(true,_data_only);
    if (q)
      output_push_batch(i,q);
  }
  output_push_batch(n-1,batch);
}
#endif

void
Duplicate::push(int, Packet *p)
{
  int n = noutputs();
  for (int i = 0; i < n - 1; i++)
    if (Packet *q = p->duplicate())
      output(i).push(q);
  output(n - 1).push(p);
}


CLICK_ENDDECLS
EXPORT_ELEMENT(Duplicate)
ELEMENT_MT_SAFE(Duplicate)