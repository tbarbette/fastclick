// -*- c-basic-offset: 4 -*-
/*
 * batchstats.{cc,hh} -- batch statistics counter
 * Tom Barbette
 *
 * Copyright (c) 2016 University of Liege
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
#include "batchstats.hh"
#include <click/string.hh>
#include <click/straccum.hh>
#include <click/args.hh>

CLICK_DECLS

BatchStats::BatchStats()
{
}

BatchStats::~BatchStats()
{
}
int
BatchStats::configure(Vector<String> &conf, ErrorHandler *errh)
{

    if (Args(conf, this, errh)
	.complete() < 0)
	return -1;

    return 0;
}

int
BatchStats::initialize(ErrorHandler *errh)
{
    stats.initialize(get_passing_threads(),Vector<int>(MAX_BATCH_SIZE,0));
    return 0;
}

void
BatchStats::cleanup(CleanupStage)
{

}

Packet*
BatchStats::simple_action(Packet* p)
{
    (*stats)[1]++;
    return p;
}

#if HAVE_BATCH
PacketBatch*
BatchStats::simple_action_batch(PacketBatch* b)
{
    (*stats)[b->count()]++;
    return b;
}
#endif

String
BatchStats::read_handler(Element *e, void *thunk)
{
    BatchStats *fd = static_cast<BatchStats *>(e);
    switch ((intptr_t)thunk) {
      case H_MEDIAN: {
          Vector<int> sums(MAX_BATCH_SIZE,0);
          int max_batch_v = -1;
          int max_batch_index = -1;
          for (unsigned i = 0; i < fd->stats.weight(); i++) {
              for (unsigned j = 0; j < MAX_BATCH_SIZE; j++) {
                  sums[j] += fd->stats.get_value(i)[j];
                  if (sums[j] > max_batch_v) {
                      max_batch_v = sums[j];
                      max_batch_index = j;
                  }
              }
          }
          return String(max_batch_index);
      }
      case H_AVERAGE: {
          int count = 0;
          int total = 0;
          for (unsigned i = 0; i < fd->stats.weight(); i++) {
              for (unsigned j = 0; j < MAX_BATCH_SIZE; j++) {
                  total += fd->stats.get_value(i)[j] * j;
                  count += fd->stats.get_value(i)[j];
              }
          }
          return String(total/count);
      }
      case H_DUMP: {
          StringAccum s;
          Vector<int> sums(MAX_BATCH_SIZE,0);
          for (unsigned i = 0; i < fd->stats.weight(); i++) {
              for (unsigned j = 0; j < MAX_BATCH_SIZE; j++) {
                  sums[j] += fd->stats.get_value(i)[j];
                  if (i == fd->stats.weight() - 1 && sums[j] != 0)
                      s << j << ": " << sums[j] << "\n";
              }
          }
          return s.take_string();
      }
      default:
    return "<error>";
    }
}

void
BatchStats::add_handlers()
{
    add_read_handler("median", read_handler, H_MEDIAN, Handler::f_expensive);
    add_read_handler("average", read_handler, H_AVERAGE, Handler::f_expensive);
    add_read_handler("dump", read_handler, H_DUMP, Handler::f_expensive);
}


CLICK_ENDDECLS

ELEMENT_REQUIRES(batch)
EXPORT_ELEMENT(BatchStats)
ELEMENT_MT_SAFE(BatchStats)
