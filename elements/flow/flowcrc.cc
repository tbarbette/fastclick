/*
 * FlowIDSMatcher.{cc,hh} -- element classifies packets by contents
 * using regular expression matching
 *
 * Element originally imported from http://www.openboxproject.org/
 *
 * Computational batching support by Tom Barbette
 *
 * Copyright (c) 2017 University of Liege
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
#include <click/glue.hh>
#include <click/error.hh>
#include <click/args.hh>
#include <click/confparse.hh>
#include <click/router.hh>
#include "flowcrc.hh"

CLICK_DECLS


FlowCRC::FlowCRC()
{
}

FlowCRC::~FlowCRC() {
}

int
FlowCRC::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}



int
FlowCRC::process_data(fcb_crc* fcb, FlowBufferChunkIter& iterator) {
    unsigned crc = fcb->crc;
    while (iterator) {
        auto chunk = *iterator;
        crc = update_crc(crc, (char *) chunk.bytes, chunk.length);
    }
    fcb->crc = crc;
    return 0;
}


String
FlowCRC::read_handler(Element *e, void *thunk)
{
    FlowCRC *c = (FlowCRC *)e;
    switch ((intptr_t)thunk) {
      default:
          return "<error>";
    }
}

int
FlowCRC::write_handler(const String &in_str, Element *e, void *thunk, ErrorHandler *errh)
{
    FlowCRC *c = (FlowCRC *)e;
    switch ((intptr_t)thunk) {
        default:
            return errh->error("<internal>");
    }
}


CLICK_ENDDECLS
EXPORT_ELEMENT(FlowCRC)
ELEMENT_REQUIRES(userlevel)
ELEMENT_MT_SAFE(FlowCRC)
