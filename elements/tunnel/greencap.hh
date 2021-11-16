// -*- mode: c++; c-basic-offset: 2 -*-
/*
 * greencap.hh -- element encapsulates packet in GRE header
 * Mark Huang <mlhuang@cs.princeton.edu>
 *
 * Copyright (c) 2003  The Trustees of Princeton University (Trustees).
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
 * $Id: greencap.hh,v 1.6 2007/06/27 15:55:49 eddietwo Exp $
 */

#ifndef CLICK_GREENCAP_HH
#define CLICK_GREENCAP_HH
#include <click/element.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#include "gre.h"
CLICK_DECLS

/*
=c

GREEncap(PROTOCOL, I<KEYWORDS>)

=s tunnel

encapsulates packets in static GRE header

=d

Encapsulates each incoming packet in a GRE packet with protocol
PROTOCOL.
This is most useful for GRE-in-IP encapsulation.

Keyword arguments are:

=over 8

=item CHECKSUM

Boolean. If true, sets the Checksum Present bit to one and fills the
Checksum field with the IP (one's complement) checksum sum of the all
the 16 bit words in the GRE header and the payload packet.  For
purposes of computing the checksum, the value of the checksum field is
zero.

=back

The StripGREHeader element can be used by the receiver to get rid
of the encapsulation header.

=e

Wraps packets in a GRE-in-IP header specifying IP protocol 47
(GRE-in-IP), with source 18.26.4.24 and destination 140.247.60.147:

  GREEncap(0x0800)
  IPEncap(47, 18.26.4.24, 140.247.60.147)

=a CheckGREHeader, StripGREHeader */

class GREEncap : public Element { public:
  
  GREEncap();
  ~GREEncap();
  
  const char *class_name() const override		{ return "GREEncap"; }
  const char *port_count() const override		{ return PORTS_1_1; }
  const char *processing() const override		{ return AGNOSTIC; }
  // this element requires AlignmentInfo
  const char *flags() const			{ return "A"; }
  
  int configure(Vector<String> &, ErrorHandler *);

private:

  click_gre _greh;	// GRE header to append to each packet
  int _len;		// length of GRE header
#if HAVE_FAST_CHECKSUM && FAST_CHECKSUM_ALIGNED
  bool _aligned;
#endif

  Packet *simple_action(Packet *);
  
};

CLICK_ENDDECLS
#endif
