// -*- c-basic-offset: 4 -*-
/*
 * packetmemstats.{cc,hh} -- packet memory statistics
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

#include "packetmemstats.hh"

CLICK_DECLS

PacketMemStats::PacketMemStats() : PacketMemStats(DEF_ALIGN)
{

}

PacketMemStats::PacketMemStats(unsigned align) : _align(align), _stats()
{

}

PacketMemStats::~PacketMemStats()
{
    for (unsigned i = 0; i < _stats.weight(); i ++) {
        delete _stats.get();
    }
}

int
PacketMemStats::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read("ALIGN_STRIDE", _align)
        .complete() < 0)
        return -1;

    return 0;
}

int
PacketMemStats::initialize(ErrorHandler *errh)
{
    for (unsigned i = 0; i < _stats.weight(); i ++) {
        _stats.set_value_for_thread(i, new PacketMemStats::MemStats());
    }
    return 0;
}

void
PacketMemStats::cleanup(CleanupStage)
{
    return;
}

Packet*
PacketMemStats::simple_action(Packet *p)
{
    if (!p) {
        return 0;
    }

    PacketMemStats::MemStats *s = *_stats;
    if (mem_is_aligned((void *)p->data(), _align)) {
        s->update_aligned_pkts();
    }
    s->update_total_pkts();

    return p;
}

#if HAVE_BATCH
PacketBatch*
PacketMemStats::simple_action_batch(PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET(PacketMemStats::simple_action, batch);
    return batch;
}
#endif

void
PacketMemStats::get_counter_status(uint64_t &tot_aligned, uint64_t &tot_unaligned, uint64_t &total)
{
    for (unsigned i = 0; i < _stats.weight(); i++) {
        tot_aligned += _stats.get_value(i)->get_aligned_pkts();
        tot_unaligned += _stats.get_value(i)->get_unaligned_pkts();
        total += _stats.get_value(i)->get_total_pkts();
    }

    assert(total == (tot_aligned + tot_unaligned));
}

String
PacketMemStats::read_handler(Element *e, void *thunk)
{
    PacketMemStats *fd = static_cast<PacketMemStats *>(e);

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
        case h_aligned_ratio: {
            return (total > 0) ? String(((double)tot_aligned/(double)total)*100.0f) : "0";
        }
        case h_unaligned_ratio: {
            return (total > 0) ? String(((double)tot_unaligned/(double)total)*100.0f) : "0";
        }
        case h_align_stride: {
            return String(fd->get_align());
        }
        default: {
            return "<error>";
        }
    }
}

int
PacketMemStats::write_handler(const String &input, Element *e, void *thunk, ErrorHandler *errh)
{
    PacketMemStats *fd = static_cast<PacketMemStats *>(e);
    if (!fd) {
        return -1;
    }

    switch((uintptr_t) thunk) {
        case h_align_stride: {
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
PacketMemStats::add_handlers()
{
    add_read_handler("align_stride", read_handler, h_align_stride, Handler::f_expensive);
    add_read_handler("aligned_pkts", read_handler, h_aligned, Handler::f_expensive);
    add_read_handler("unaligned_pkts", read_handler, h_unaligned, Handler::f_expensive);
    add_read_handler("total_pkts", read_handler, h_total, Handler::f_expensive);
    add_read_handler("aligned_pkts_ratio", read_handler, h_aligned_ratio, Handler::f_expensive);
    add_read_handler("unaligned_pkts_ratio", read_handler, h_unaligned_ratio, Handler::f_expensive);

    add_write_handler("align_stride", write_handler, h_align_stride);
}


CLICK_ENDDECLS

ELEMENT_REQUIRES(batch)
EXPORT_ELEMENT(PacketMemStats)
ELEMENT_MT_SAFE(PacketMemStats)
