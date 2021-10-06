/*
 * ctxcrc.{cc,hh}
 *
 * Tom Barbette
 *
 * Copyright (c) 2017-2018 University of Liege
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
#include <click/crc32.h>
#include "ctxcrc.hh"

CLICK_DECLS


CTXCRC::CTXCRC() : _add(true)
{
}

CTXCRC::~CTXCRC() {
}

int
CTXCRC::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}



int
CTXCRC::process_data(fcb_crc* fcb, FlowBufferChunkIter& iterator) {
    unsigned crc = fcb->crc;
    unsigned remain = fcb->remain;
    unsigned remainder = fcb->remainder;
    while (iterator) {
        auto chunk = *iterator;
        if (_add) {
            unsigned* b;
            int l =  chunk.length;
            if (remain) {
                unsigned char* br = (unsigned char *)chunk.bytes;
                do {
                    remainder += *br;
                    br++;
                    l--;
                    if (--remain == 0)
                        break;
                    remainder <<= 8;
                } while(1);
                b = (unsigned*)br;
            } else {
                b = (unsigned*)chunk.bytes;;
            }
            int i = 0;
            for (i = 0; i < l / 4; i ++) {
                crc += *b;
                ++b;
            }
            remain = l - (i*4);
            for (i = 0; i < remain; i++) {
                remainder += *b << 8;
            }
        } else {
            crc = update_crc(crc, (char *) chunk.bytes, chunk.length);
        }
        ++iterator;
    }
    fcb->crc = crc;
    fcb->remain = remain;
    fcb->remainder = remainder;
    return 0;
}


String
CTXCRC::read_handler(Element *e, void *thunk)
{
    CTXCRC *c = (CTXCRC *)e;
    switch ((intptr_t)thunk) {
      default:
          return "<error>";
    }
}

int
CTXCRC::write_handler(const String &in_str, Element *e, void *thunk, ErrorHandler *errh)
{
    CTXCRC *c = (CTXCRC *)e;
    switch ((intptr_t)thunk) {
        default:
            return errh->error("<internal>");
    }
}


CLICK_ENDDECLS
EXPORT_ELEMENT(CTXCRC)
ELEMENT_REQUIRES(userlevel)
ELEMENT_MT_SAFE(CTXCRC)
