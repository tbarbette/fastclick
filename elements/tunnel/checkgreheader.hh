// -*- mode: c++; c-basic-offset: 2 -*-
/*
 * checkgreheader.hh -- element checks GRE header for correctness
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
 * $Id: checkgreheader.hh,v 1.6 2007/06/27 15:55:49 eddietwo Exp $
 */

#ifndef CLICK_CHECKGREHEADER_HH
#define CLICK_CHECKGREHEADER_HH
#include <click/element.hh>
#include <click/atomic.hh>
CLICK_DECLS

/*
=c

CheckGREHeader([I<keywords> OFFSET, CHECKSUM, KEY, SEQUENCE, VERBOSE, DETAILS])

=s tunnel

checks GRE header

=d

Input packets should have GRE headers starting OFFSET bytes in. Default OFFSET
is zero. Checks that the packet's length is reasonable, and that the GRE
version, checksum, key, and sequence fields are valid.

CheckGREHeader emits valid packets on output 0. Invalid packets are pushed out
on output 1, unless output 1 was unused; if so, drops invalid packets.

CheckGREHeader prints a message to the console the first time it encounters an
incorrect GRE packet (but see VERBOSE below).

Keyword arguments are:

=over 5

=item CHECKSUM

Boolean. If true, then check each header's checksum for presence and
validity. Default is false.

=item KEY

Unsigned. If not 0, check each header's key field for presence and
validity (equal to this value). Default is false.

=item SEQUENCE

Boolean. If true, then check each header's sequence number for
presence and validity. Default is false.

=item OFFSET

Unsigned integer. Byte position at which the IP header begins. Default is 0.

=item VERBOSE

Boolean. If it is true, then a message will be printed for every erroneous
packet, rather than just the first. False by default.

=item DETAILS

Boolean. If it is true, then CheckGREHeader will maintain detailed counts of
how many packets were dropped for each possible reason, accessible through the
C<drop_details> handler. False by default.

=back

=h drops read-only

Returns the number of incorrect packets CheckGREHeader has seen.

=h drop_details read-only

Returns a text file showing how many erroneous packets CheckGREHeader has seen,
subdivided by error. Only available if the DETAILS keyword argument was true.

=a GREEncap, StripGREHeader */

class CheckGREHeader : public Element { public:

  CheckGREHeader();
  ~CheckGREHeader();
  
  const char *class_name() const override		{ return "CheckGREHeader"; }
  const char *port_count() const override		{ return "1/1-2"; }
  const char *processing() const override		{ return AGNOSTIC; }
  // this element requires AlignmentInfo
  const char *flags() const			{ return "A"; }
  
  int configure(Vector<String> &, ErrorHandler *);
  void add_handlers();

  Packet *simple_action(Packet *);

 private:
  
  unsigned _offset;
  
  bool _checksum;
  uint32_t _key, _seq;
  bool _checkseq;
#if HAVE_FAST_CHECKSUM && FAST_CHECKSUM_ALIGNED
  bool _aligned : 1;
#endif
  bool _verbose;
  
  uatomic32_t _drops;
  uatomic32_t *_reason_drops;

  bool _strip;

  enum Reason {
    MINISCULE_PACKET,
    BAD_VERSION,
    BAD_HLEN,
    BAD_CHECKSUM,
    BAD_KEY,
    BAD_SEQ,
    NREASONS
  };
  static const char * const reason_texts[NREASONS];
  
  Packet *drop(Reason, Packet *);
  static String read_handler(Element *, void *);

};

CLICK_ENDDECLS
#endif
