/*
 * stripgreheader.hh -- element removes GRE header
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
 * $Id: stripgreheader.hh,v 1.4 2006/02/24 17:18:18 eddietwo Exp $
 */

#ifndef CLICK_STRIPGREHEADER_HH
#define CLICK_STRIPGREHEADER_HH
#include <click/element.hh>
CLICK_DECLS

/*
 * =c
 * StripGREHeader()
 * =s tunnel
 * strips outermost GRE header
 * =d
 * Removes the outermost GRE header from GRE packets.
 *
 * =a GREEncap, CheckGREHeader
 */

class StripGREHeader : public Element {

 public:
  
  StripGREHeader();
  ~StripGREHeader();
  
  const char *class_name() const override		{ return "StripGREHeader"; }
  const char *port_count() const override		{ return PORTS_1_1; }

  Packet *simple_action(Packet *);
  
};

CLICK_ENDDECLS
#endif
