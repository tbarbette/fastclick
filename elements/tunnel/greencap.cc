// -*- mode: c++; c-basic-offset: 2 -*-
/*
 * greencap.cc -- element encapsulates packet in GRE header
 * Mark Huang <mlhuang@cs.princeton.edu>
 *
 * Copyright (c) 2004  The Trustees of Princeton University (Trustees).
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
 *
 * $Id: greencap.cc,v 1.5 2005/09/19 22:45:07 eddietwo Exp $
 */

#include <click/config.h>
#include "greencap.hh"
#include "gre.h"
#include <click/ipaddress.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/standard/alignmentinfo.hh>
CLICK_DECLS

GREEncap::GREEncap()
{
}

GREEncap::~GREEncap()
{
}

int
GREEncap::configure(Vector<String> &conf, ErrorHandler *errh)
{
  uint16_t protocol;
  uint32_t key = 0;
  bool checksum = false, seq = false;
  uint32_t *options = _greh.options;

  if (cp_va_kparse(conf, this, errh,
		   "PROTOCOL", cpkP+cpkM, cpShort, &protocol,
		   "CHECKSUM", 0, cpBool, &checksum,
		   "KEY", 0, cpUnsigned, &key,
		   "SEQUENCE", 0, cpBool, &seq,
		   cpEnd) < 0)
    return -1;

  memset(&_greh, 0, sizeof(_greh));
  _len = 4;

  _greh.protocol = htons(protocol);
  if (checksum) {
    _greh.flags |= htons(GRE_CP);
    options++;
  }
  if (key) {
    _greh.flags |= htons(GRE_KP);
    *options++ = htonl(key);
  }
  if (seq) {
    _greh.flags |= htons(GRE_SP);
    *options++ = htonl(0);
  }
  _len = (char *)options - (char *)&_greh;

#if HAVE_FAST_CHECKSUM && FAST_CHECKSUM_ALIGNED
  // check alignment
  {
    int ans, c, o;
    ans = AlignmentInfo::query(this, 0, c, o);
    _aligned = (ans && c == 4 && o == 0);
    if (!_aligned)
      errh->warning("IP header unaligned, cannot use fast IP checksum");
    if (!ans)
      errh->message("(Try passing the configuration through `click-align'.)");
  }
#endif

  return 0;
}

Packet *
GREEncap::simple_action(Packet *p_in)
{
  WritablePacket *p = p_in->push(_len);
  if (!p) return 0;

  click_gre *greh = reinterpret_cast<click_gre *>(p->data());
  uint32_t *options = _greh.options;

  if (_greh.flags & htons(GRE_CP))
    *options++ = 0;
  if (_greh.flags & htons(GRE_KP))
    options++;
  if (_greh.flags & htons(GRE_SP))
    *options++ = htonl(ntohl(*options) + 1);
  assert(((char *)options - (char *)&_greh) == _len);

  memcpy(greh, &_greh, _len);

  if (_greh.flags & htons(GRE_CP)) {
    uint16_t checksum;
#if HAVE_FAST_CHECKSUM && FAST_CHECKSUM_ALIGNED
    if (_aligned)
      checksum = ip_fast_csum(p->data(), p->length() >> 2);
    else
      checksum = click_in_cksum(p->data(), p->length());
#elif HAVE_FAST_CHECKSUM
    checksum = ip_fast_csum(p->data(), p->length() >> 2);
#else
    checksum = click_in_cksum(p->data(), p->length());
#endif
    memcpy(&greh->options, &checksum, sizeof(checksum));
  }

  return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(GREEncap)
ELEMENT_MT_SAFE(GREEncap)
