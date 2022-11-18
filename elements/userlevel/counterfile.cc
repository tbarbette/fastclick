/*
 * counterfile.{cc,hh} -- element counts packets
 * Eddie Kohler, Tom Barbette, Piotr Jurkiewicz
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2008 Regents of the University of California
 * Copyright (c) 2016 University of Liege
 * Copyright (c) 2018 AGH University of Science and Technology
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
#include "counterfile.hh"
#include <click/error.hh>
#include <click/args.hh>
#include <fcntl.h>
#include <sys/mman.h>
CLICK_DECLS

CounterFile::CounterFile()
: _batch_precise(false), _filename(), _mmapped_stats((stats_atomic *) MAP_FAILED)
{
}

CounterFile::~CounterFile()
{
}

int
CounterFile::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_mp("FILENAME", FilenameArg(), _filename)
            .read("BATCH_PRECISE", _batch_precise)
            .complete() < 0)
        return -1;

    return 0;
}

int
CounterFile::initialize(ErrorHandler *errh)
{
    int fd = open(_filename.c_str(), O_RDWR | O_CREAT, 0644);

    if (fd < 0) {
        int e = -errno;
        errh->error("%s: %s", _filename.c_str(), strerror(-e));
        return e;
    }

    int r = ftruncate(fd, sizeof(stats_atomic));
    if (r != 0) {
        return -errno;
    }

    void *mmap_data = mmap(0, sizeof(stats_atomic), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);

    if (mmap_data == MAP_FAILED) {
        int e = -errno;
        errh->error("%s mmap: %s", _filename.c_str(), strerror(-e));
        close(fd);
        return e;
    }

    close(fd);

    _mmapped_stats = static_cast<stats_atomic *>(mmap_data);

    return 0;
}

void
CounterFile::cleanup(CleanupStage)
{
    void *mmap_data = static_cast<void *>(_mmapped_stats);

    if (mmap_data != MAP_FAILED)
        munmap(mmap_data, sizeof(stats_atomic));
}

bool CounterFile::do_mt_safe_check(ErrorHandler*) {
    return true;
}

enum { H_COUNT, H_BYTE_COUNT, H_RESET };

String
CounterFile::read_handler(Element *e, void *thunk)
{
    CounterFile *c = (CounterFile *)e;

    switch ((intptr_t)thunk) {
    case H_COUNT:
        return String(c->count());
    case H_BYTE_COUNT:
        return String(c->byte_count());
    default:
        return "<error>";
    }
}

int
CounterFile::write_handler(const String &, Element *e, void *thunk, ErrorHandler *errh)
{
    CounterFile *c = (CounterFile *)e;
    switch ((intptr_t)thunk) {
    case H_RESET:
        c->reset();
        return 0;
    default:
        return errh->error("<internal>");
    }
}

void
CounterFile::add_handlers()
{
    add_read_handler("count", CounterFile::read_handler, H_COUNT);
    add_read_handler("byte_count", CounterFile::read_handler, H_BYTE_COUNT);
    add_write_handler("reset", CounterFile::write_handler, H_RESET, Handler::f_button);
    add_write_handler("reset_counts", CounterFile::write_handler, H_RESET, Handler::f_button | Handler::f_uncommon);
}

Packet*
CounterFile::simple_action(Packet *p)
{
    _mmapped_stats->_count++;
    _mmapped_stats->_byte_count += p->length();
    return p;
}

#if HAVE_BATCH
PacketBatch*
CounterFile::simple_action_batch(PacketBatch *batch)
{
    if (unlikely(_batch_precise)) {
        FOR_EACH_PACKET(batch, p)
                        CounterFile::simple_action(p);
        return batch;
    }

    counter_int_type bc = 0;
    FOR_EACH_PACKET(batch, p) {
        bc += p->length();
    }
    _mmapped_stats->_count += batch->count();
    _mmapped_stats->_byte_count += bc;
    return batch;
}
#endif

void
CounterFile::reset()
{
    _mmapped_stats->_count = 0;
    _mmapped_stats->_byte_count = 0;
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(CounterFile)
ELEMENT_MT_SAFE(CounterFile)
