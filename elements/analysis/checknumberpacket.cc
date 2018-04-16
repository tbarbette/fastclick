/*
 * checknumberpacket.{cc,hh} -- Check number inside packet
 * Tom Barbette
 * Support for network order numbering by Georgios Katsikas
 *
 * Copyright (c) 2015-2017 University of Liège
 * Copyright (c) 2018 RISE SICS
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

#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>

#include "checknumberpacket.hh"

CLICK_DECLS

CheckNumberPacket::CheckNumberPacket() : _offset(40), _net_order(false), _count(0) {
}

CheckNumberPacket::~CheckNumberPacket() {
}

int CheckNumberPacket::configure(Vector<String> &conf, ErrorHandler *errh) {
    if (Args(conf, this, errh)
        .read_p("OFFSET", _offset)
        .read("NET_ORDER", _net_order)
        .read_p("COUNT", _count)
        .complete() < 0)
        return -1;

    if (_count > 0)
        _numbers.resize(_count, 0);
    return 0;
}

inline int CheckNumberPacket::smaction(Packet *p) {
    if ((int)p->length() < _offset + 8) {
        return 1;
    }
    uint64_t n = NumberPacket::read_number_of_packet(p, _offset, _net_order);
    if (n >= _count) {
        click_chatter(
          "%p{element}: %" PRIu64 " out of scope (count is %" PRIu64 ") !",
          this, n, _count
        );
        return 1;
    } else {
        _numbers[n]++;
    }
    return 0;
}

void
CheckNumberPacket::push(int, Packet *p) {
    checked_output_push(smaction(p), p);
}

#if HAVE_BATCH
void
CheckNumberPacket::push_batch(int, PacketBatch *batch) {
    CLASSIFY_EACH_PACKET(2,smaction,batch,checked_output_push_batch);
}
#endif

String
CheckNumberPacket::read_handler(Element *e, void *thunk)
{
    CheckNumberPacket *fd = static_cast<CheckNumberPacket *>(e);
    switch ((intptr_t)thunk) {
      case H_MAX: {
        StringAccum s;
        int max = -1;
        int imax = -1;
        for (unsigned j = 0; j < (unsigned)fd->_numbers.size(); j++) {
            if (fd->_numbers[j] > max) {
                imax=j;
                max = fd->_numbers[j];
            }
        }
        return "[" + String(imax) + "] : " + String(max);
      }
      case H_MIN: {
          StringAccum s;
          int min = INT_MAX;
          int imin = -1;
          for (unsigned j = 0; j < (unsigned)fd->_numbers.size(); j++) {
              if (fd->_numbers[j] < min) {
                  imin=j;
                  min = fd->_numbers[j];
              }
          }
          return "[" + String(imin) + "] : " + String(min);
        }
      case H_COUNT: {
          return String(fd->_count);
      }
      case H_DUMP: {
          StringAccum s;
          for (unsigned j = 0; j < (unsigned)fd->_numbers.size(); j++) {
                  s << j << ": " << fd->_numbers[j] << "\n";
          }
          return s.take_string();
      }
      default:
    return "<error>";
    }
}

int
CheckNumberPacket::write_handler(const String &s_in, Element *e, void *thunk, ErrorHandler *)
{
    CheckNumberPacket *fd = static_cast<CheckNumberPacket *>(e);
    String s = cp_uncomment(s_in);
    switch ((intptr_t)thunk) {
      case H_COUNT: {
          uint64_t n;
          if (IntArg().parse(s, n)) {
              fd->_count = n;
              fd->_numbers.resize(n,0);
              return 0;
          }
      }
      default:
        return -EINVAL;
    }
}


void
CheckNumberPacket::add_handlers()
{
    add_read_handler("max", read_handler, H_MAX, Handler::f_expensive);
    add_read_handler("min", read_handler, H_MIN, Handler::f_expensive);
    add_read_handler("count", read_handler, H_COUNT, 0);
    add_read_handler("dump", read_handler, H_DUMP, Handler::f_expensive);

    add_write_handler("count", write_handler, H_COUNT);
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(CheckNumberPacket)
