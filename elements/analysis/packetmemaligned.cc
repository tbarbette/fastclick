// -*- c-basic-offset: 4 -*-
/*
 * packetmemaligned.{cc,hh} -- memory alignment statistics
 * Georgios Katsikas
 *
 * Copyright (c) 2019 KTH Royal Institute of Technology
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
#include <click/string.hh>
#include <click/straccum.hh>
#include <click/args.hh>

#include "packetmemaligned.hh"

CLICK_DECLS

PacketMemAligned::PacketMemAligned() : PacketMemAligned(DEF_ALIGN)
{

}

PacketMemAligned::PacketMemAligned(unsigned align) : _stats(), _align(align)
{

}

PacketMemAligned::~PacketMemAligned()
{
    for (int i = 0; i < _stats.weight(); i ++) {
        delete _stats.get();
    }
}

int
PacketMemAligned::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read("ALIGN_STRIDE", _align)
        .complete() < 0)
        return -1;

    return 0;
}

int
PacketMemAligned::initialize(ErrorHandler *errh)
{
    for (int i = 0; i < _stats.weight(); i ++) {
        _stats.set_value_for_thread(i, new PacketMemAligned::MemStats());
    }
    return 0;
}

void
PacketMemAligned::cleanup(CleanupStage)
{
    return;
}

Packet*
PacketMemAligned::simple_action(Packet *p)
{
    if (!p) {
        return 0;
    }

    PacketMemAligned::MemStats *s = *_stats;
    if (mem_is_aligned((void *)p->data(), _align)) {
        s->update_aligned_pkts();
    }
    s->update_total_pkts();

    return p;
}

#if HAVE_BATCH
PacketBatch*
PacketMemAligned::simple_action_batch(PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET(PacketMemAligned::simple_action, batch);
    return batch;
}
#endif

void
PacketMemAligned::get_counter_status(uint64_t &tot_aligned, uint64_t &tot_unaligned, uint64_t &total)
{
    for (unsigned i = 0; i < _stats.weight(); i++) {
        tot_aligned += _stats.get_value(i)->get_aligned_pkts();
        tot_unaligned += _stats.get_value(i)->get_unaligned_pkts();
        total += _stats.get_value(i)->get_total_pkts();
    }

    assert(total == (tot_aligned + tot_unaligned));
}

String
PacketMemAligned::read_handler(Element *e, void *thunk)
{
    PacketMemAligned *fd = static_cast<PacketMemAligned *>(e);

    uint64_t tot_aligned = 0, tot_unaligned = 0, total = 0;
    fd->get_counter_status(tot_aligned, tot_unaligned, total);

    switch ((intptr_t)thunk) {
        case h_aligned: {
            return String(tot_aligned);
        }
        case h_unaligned: {
            return String(tot_unaligned);
        }
        case h_total: {
            return String(total);
        }
        case h_align: {
            return String(fd->get_align());
        }
        default: {
            return "<error>";
        }
    }
}

int
PacketMemAligned::write_handler(const String &input, Element *e, void *thunk, ErrorHandler *errh)
{
    PacketMemAligned *fd = static_cast<PacketMemAligned *>(e);
    if (!fd) {
        return -1;
    }

    switch((uintptr_t) thunk) {
        case h_align: {
            if (input.empty()) {
                return -1;
            }
            fd->set_align(atoi(input.c_str()));
            return 0;
        }
    }

    return -1;
}

void
PacketMemAligned::add_handlers()
{
    add_read_handler("align_stride", read_handler, h_align, Handler::f_expensive);
    add_read_handler("aligned_pkts", read_handler, h_aligned, Handler::f_expensive);
    add_read_handler("unaligned_pkts", read_handler, h_unaligned, Handler::f_expensive);
    add_read_handler("total_pkts", read_handler, h_total, Handler::f_expensive);
    add_write_handler("align_stride", write_handler, h_align);
}


CLICK_ENDDECLS

ELEMENT_REQUIRES(batch)
EXPORT_ELEMENT(PacketMemAligned)
ELEMENT_MT_SAFE(PacketMemAligned)
