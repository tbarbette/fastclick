// -*- mode: c++; c-basic-offset: 2 -*-
/*
 * stripgreheader.cc -- element removes GRE header
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
 * $Id: stripgreheader.cc,v 1.4 2005/09/19 22:45:07 eddietwo Exp $
 */

#include <click/config.h>
#include "stripgreheader.hh"
#include "gre.h"
CLICK_DECLS

StripGREHeader::StripGREHeader()
{
}

StripGREHeader::~StripGREHeader()
{
}

Packet *
StripGREHeader::simple_action(Packet *p)
{
  const click_gre *greh = reinterpret_cast<const click_gre *>(p->data());
  uint16_t flags;
  unsigned hlen = 4;

  // avoid alignment issues
  memcpy(&flags, &greh->flags, sizeof(greh->flags));

  if (flags & htons(GRE_CP))
    hlen += 4;
  if (flags & htons(GRE_KP))
    hlen += 4;
  if (flags & htons(GRE_SP))
    hlen += 4;

  p->pull(hlen);
  return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(StripGREHeader)
ELEMENT_MT_SAFE(StripGREHeader)
