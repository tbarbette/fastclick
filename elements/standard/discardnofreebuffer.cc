/*
 * discardnofreebuffer.{cc,hh} -- element pulls packets, doesn't throw them away
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
#include "discardnofreebuffer.hh"
#include <click/standard/scheduleinfo.hh>
CLICK_DECLS

DiscardNoFreeBuffer::DiscardNoFreeBuffer()
  : _task(this)
{
}

int
DiscardNoFreeBuffer::initialize(ErrorHandler *errh)
{
  if (input_is_pull(0))
    ScheduleInfo::initialize_task(this, &_task, errh);
  return 0;
}

void
DiscardNoFreeBuffer::push(int, Packet *p)
{
    p->reset_buffer();
    p->kill();
}

#if HAVE_BATCH
void
DiscardNoFreeBuffer::push_batch(int, PacketBatch *batch)
{
    FOR_EACH_PACKET(batch,p)
        p->reset_buffer();
    batch->fast_kill();
}
#endif

bool
DiscardNoFreeBuffer::run_task(Task *)
{
  Packet *p = input(0).pull();	// Not killed!
  _task.fast_reschedule();
  return p != 0;
}

void
DiscardNoFreeBuffer::add_handlers()
{
  if (input_is_pull(0))
    add_task_handlers(&_task);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(!dpdk-packet)
EXPORT_ELEMENT(DiscardNoFreeBuffer)
ELEMENT_MT_SAFE(DiscardNoFreeBuffer)
