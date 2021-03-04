// -*- mode: c++; c-basic-offset: 2 -*-
/*
 * checkgreheader.cc -- element checks GRE header for correctness
 * Mark Huang <mlhuang@cs.princeton.edu>
 * Tom Barbette
 *
 * Copyright (c) 2004  The Trustees of Princeton University (Trustees).
 * Copyright (c) 2018  KTH Royal Institute of Technology
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
 * $Id: checkgreheader.cc,v 1.5 2005/09/19 22:45:06 eddietwo Exp $
 */

#include <click/config.h>
#include "checkgreheader.hh"
#include "gre.h"
#include <click/glue.hh>
#include <click/confparse.hh>
#include <click/straccum.hh>
#include <click/error.hh>
#include <click/standard/alignmentinfo.hh>
#ifdef CLICK_LINUXMODULE
# include <net/checksum.h>
#endif
CLICK_DECLS

const char * const CheckGREHeader::reason_texts[NREASONS] = {
  "tiny packet", "bad GRE version", "bad GRE header length",
  "bad GRE checksum", "invalid key", "out of sequence"
};

CheckGREHeader::CheckGREHeader()
  : _offset(0), _checksum(false), _key(0), _checkseq(false), _reason_drops(0), _strip(false)
{
  _drops = 0;
}

CheckGREHeader::~CheckGREHeader()
{
  delete[] _reason_drops;
}

int
CheckGREHeader::configure(Vector<String> &conf, ErrorHandler *errh)
{
  _offset = 0;
  bool details = false;

  if (cp_va_kparse_remove_keywords(conf, this, errh,
		"OFFSET", 0, cpUnsigned, &_offset,
		"VERBOSE", 0, cpBool, &_verbose,
		"DETAILS", 0, cpBool, &details,
		"CHECKSUM", 0, cpBool, &_checksum,
		"KEY", 0, cpUnsigned, &_key,
		"SEQUENCE", 0, cpBool, &_checkseq,
        "STRIP", 0, cpBool, &_strip,
		cpEnd) < 0)
    return -1;

  if (details)
    _reason_drops = new uatomic32_t[NREASONS];

#if HAVE_FAST_CHECKSUM && FAST_CHECKSUM_ALIGNED
  // check alignment
  if (_checksum) {
    int ans, c, o;
    ans = AlignmentInfo::query(this, 0, c, o);
    o = (o + 4 - (_offset % 4)) % 4;
    _aligned = (ans && c == 4 && o == 0);
    if (!_aligned)
      errh->warning("IP header unaligned, cannot use fast IP checksum");
    if (!ans)
      errh->message("(Try passing the configuration through `click-align'.)");
  }
#endif

  if (_checkseq)
    _seq = 0;

  return 0;
}

Packet *
CheckGREHeader::drop(Reason reason, Packet *p)
{
  if (_drops == 0 || _verbose)
    click_chatter("GRE header check failed: %s", reason_texts[reason]);
  _drops++;

  if (_reason_drops)
    _reason_drops[reason]++;
  
  if (noutputs() == 2)
    output(1).push(p);
  else
    p->kill();

  return 0;
}

Packet *
CheckGREHeader::simple_action(Packet *p)
{
  const click_gre *greh = reinterpret_cast<const click_gre *>(p->data() + _offset);
  const uint32_t *options = greh->options;
  uint16_t flags;
  uint32_t key, seq;
  unsigned plen = p->length() - _offset;
  unsigned hlen = 4;

  // cast to int so very large plen is interpreted as negative 
  if ((int)plen < (int)hlen)
    return drop(MINISCULE_PACKET, p);

  // avoid alignment issues
  memcpy(&flags, &greh->flags, sizeof(greh->flags));

  if ((flags & htons(GRE_VERSION)) != 0)
    return drop(BAD_VERSION, p);

  if (flags & htons(GRE_CP))
    hlen += 4;
  if (flags & htons(GRE_KP))
    hlen += 4;
  if (flags & htons(GRE_SP))
    hlen += 4;

  // too small header
  if ((int)plen < (int)hlen)
    return drop(BAD_HLEN, p);

  if (flags & htons(GRE_CP))
    options++;
  if (flags & htons(GRE_KP))
    memcpy(&key, options++, sizeof(key));
  if (flags & htons(GRE_SP))
    memcpy(&seq, options++, sizeof(seq));

  if (_checksum) {
    int val = -1;
    if (flags & htons(GRE_CP)) {
#if HAVE_FAST_CHECKSUM && FAST_CHECKSUM_ALIGNED
      if (_aligned)
	val = ip_fast_csum((unsigned char *)greh, plen >> 2);
      else
	val = click_in_cksum((const unsigned char *)greh, plen);
#elif HAVE_FAST_CHECKSUM
      val = ip_fast_csum((unsigned char *)greh, plen >> 2);
#else
      val = click_in_cksum((const unsigned char *)greh, plen);
#endif
    }
    // checksum not present or incorrect
    if (val != 0)
      return drop(BAD_CHECKSUM, p);
  }

  if (_key) {
    // key not present or incorrect
    if (!(flags & htons(GRE_KP)) || _key != ntohl(key))
      return drop(BAD_KEY, p);
  }

  if (_checkseq) {
    // sequence number not present or less than or equal to previous
    if (!(flags & htons(GRE_SP)) || (int)(ntohl(seq) - _seq) <= 0)
      return drop(BAD_SEQ, p);
    _seq = ntohl(seq);
  }

  if (_strip)
      p->pull(hlen);

  return(p);
}

String
CheckGREHeader::read_handler(Element *e, void *thunk)
{
  CheckGREHeader *c = reinterpret_cast<CheckGREHeader *>(e);
  switch ((intptr_t)thunk) {

   case 0:			// drops
    return String(c->_drops) + "\n";

   case 1: {			// drop_details
     StringAccum sa;
     for (int i = 0; i < NREASONS; i++)
       sa << c->_reason_drops[i] << '\t' << reason_texts[i] << '\n';
     return sa.take_string();
   }

   default:
    return String("<error>\n");

  }
}

void
CheckGREHeader::add_handlers()
{
  add_read_handler("drops", read_handler, (void *)0);
  if (_reason_drops)
    add_read_handler("drop_details", read_handler, (void *)1);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(CheckGREHeader)
