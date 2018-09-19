/*
 * gtpdecap.{cc,hh}
 * Tom Barbette
 *
 * Copyright (c) 2018 University of Liege
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
#include <clicknet/ip.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/standard/alignmentinfo.hh>
#include <clicknet/erspan.h>
#include "erspandecap.hh"
CLICK_DECLS

ERSPANDecap::ERSPANDecap() : _span_anno(true), _ts_anno(true), _direction_anno(true)
{
}

ERSPANDecap::~ERSPANDecap()
{
}

int
ERSPANDecap::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read("SPAN_ANNO", _span_anno)
	.complete() < 0)
	return -1;

    return 0;
}

int
ERSPANDecap::classify(Packet *p)
{
  const click_erspan *erspan = reinterpret_cast<const click_erspan *>(p->data());
  int sz = sizeof(click_erspan);
  if (_span_anno)
      SET_AGGREGATE_ANNO(p, (uint16_t)erspan->id_up << 8 | (uint16_t)erspan->id_down);
  int ret = 0;
  uint8_t ver = erspan->ver;
  if (ver == 0x01) {
      if (_direction_anno)
          SET_PAINT_ANNO(p, erspan->en);
  } else if (ver == 0x02) {
      const click_erspan3 *erspan3 = reinterpret_cast<const click_erspan3 *>(p->data());
      sz = sizeof(click_erspan3);
      Timestamp ts;
/*      if (erspan3->gra == 00b) {
      }*/
      if (_direction_anno)
          SET_PAINT_ANNO(p, erspan3->d);

      uint64_t gra;
      assert(erspan3->gra == 0);

      assert(erspan3->hw_down == 0);
      assert(erspan3->o == 1);
      if (erspan3->gra == 0b00) {
          gra = 100000; //100us
      } else if (erspan3->gra == 0b01) {
          gra = 100;
      } else if (erspan3->gra == 0b11) {
          gra = 1;
      }

      if (erspan3->o) {
          const click_erspan3_platform *erspan3_p = reinterpret_cast<const click_erspan3_platform *>(p->data() + sizeof(click_erspan3));
          if (erspan3_p->platf == 3) {
              const click_erspan3_platform3 *erspan3_p3 = reinterpret_cast<const click_erspan3_platform3 *>(p->data() + sizeof(click_erspan3));
              uint64_t v = ((uint64_t)ntohl(erspan3_p3->timestamp) << 32) | (uint64_t)ntohl(erspan3->timestamp);
              ts = Timestamp::make_nsec(v * gra);

              sz += sizeof(click_erspan3_platform3);
          } else {
            click_chatter("Unimplemented ERSPAN platform version %u", erspan3_p->platf);
              sz += sizeof(click_erspan3_platform);
          }
      } else {
      }
      if (_ts_anno)
          p->set_timestamp_anno(ts);
  } else if (ver == 0x00) {
      if (!_obs_warn) {
          _obs_warn = true;
          click_chatter("Obsolete ERSPAN protocol version %x !", erspan->ver);
      }
  } else {
      click_chatter("Unknown ERSPAN protocol version %x !", erspan->ver);
      return 1;
  }

  p->pull(sz);

  return 0;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(ERSPANDecap)
ELEMENT_MT_SAFE(ERSPANDecap)
