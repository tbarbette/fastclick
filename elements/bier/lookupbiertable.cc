/*
 * lookupbiertable.{cc,hh} -- element encapsulates packet in IP6 SRv6 header
 * Nicolas Rybowski
 *
 * Copyright (c) 2024 UCLouvain
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
#include <click/glue.hh>
#include <click/packet.hh>
#include <click/straccum.hh>
#include <clicknet/bier.h>
#include <clicknet/ip6.h>
#include <cstdint>
#include <click/error.hh>
#include "lookupbiertable.hh"
CLICK_DECLS

LookupBierTable::LookupBierTable() : _bfr_id(0) {
  _drops = 0;
}
LookupBierTable::~LookupBierTable() {}

int LookupBierTable::configure(Vector<String> &conf,ErrorHandler *errh) {
  Vector<String> ifaces;

  if (Args(conf, this, errh)
      .read("BIFT_ID", _bift_id)
      .read("BFR_ID", _bfr_id)
      .read_all("IFACE", ifaces)
      .complete() < 0
  )
    return -1;

  for (int i=0; i<ifaces.size(); i++) {
    Vector<String> iface = ifaces[i].split(':');
    _ifaces.insert(iface[0], atoi(iface[1].c_str()));
  }

  if (noutputs() < 2) {
    click_chatter("LookupBierTable requires at least two outputs:\n- 0 for dropped packets\n- 1 for packets destined to localhost");
    return -1;
  }
  
  return 0;
}

void LookupBierTable::drop(Packet *p) {
  if (noutputs() >= 2)
    output(0).push(p);
  else
    p->kill();
}

/** 
* 0 -> drop
* 1 -> loopback
* 2.. -> interfaces
*/
int LookupBierTable::classify(Packet *p_in) {

  unsigned short k = 0;
  uint16_t bift_id;
  IP6Address nxt;
  bitstring fbm;
  bfrid bfr_id;
  int index;
  Packet *dup;
  size_t bsl;
  String ifname;

  WritablePacket *p = (WritablePacket *)p_in->uniqueify();
  click_ip6 *ip6 = reinterpret_cast<click_ip6*>(p->data());
  click_bier *bier = reinterpret_cast<click_bier *>(p->data()+sizeof(click_ip6));
  bier->decode();
  bsl = click_bier_expand_bsl(bier);
  bitstring bs(bier->bitstring, bsl);
 
  click_chatter(
    "bift_id %05x\ntc %02x\ns %x\nttl %02x\nnibble %02x\nver %x\nbsl %x\nentropy %02x\noam %x\nrsv %x\ndscp %x\nproto %x\nbfrid %02x\nbs %s",
    bier->bier_bift_id,
    bier->bier_tc,
    bier->bier_s,
    bier->bier_ttl,
    bier->bier_nibble,
    bier->bier_version,
    bier->bier_bsl,
    bier->bier_entropy,
    bier->bier_oam,
    bier->bier_rsv,
    bier->bier_dscp,
    bier->bier_proto,
    bier->bier_bfr_id,
    bs.unparse().c_str()
  );

  // Sanity check: the BIFT ID specified in the BIER packet corresponds to the current BIFT. 
  if (bier->bier_bift_id != _bift_id) return -1;

  // RFC8279 Section 6.5 Step 2
  while (bs != 0) {
    click_chatter("bs %s", bs.unparse().c_str());

    // RFC8279 Section 6.5 Step 3: Find first node in bitstring
    while (bs[k] == 0) k++;
    bfr_id = k+1;
    click_chatter("found bdr_id %u", bfr_id);

    // RFC8279 Section 6.5 Step 4: Deliver packet to overlay
    if (bfr_id == _bfr_id){
      // Remove local bit
      bs[k] = 0;

      // Strip IPv6 header and BIER header, then push to loopback
      Packet *p2 = p->duplicate();
      p2->pull(40+click_bier_hdr_len(bier));
      output(1).push(p2); 
      continue;
    }

    // RFC8279 Section 6.5 Step 5
    // There is no need to select the correct BIFT as this element represents a single BIFT, and
    // the BIER packet's BIFT ID already has been verified earlier.

    // RFC8279 Section 6.5 Step 6
    if (_t.lookup(bfr_id, fbm, nxt, index, ifname)) {
      fbm.resize(bsl);
  
      // RFC8279 Section 6.5 Step 7
      WritablePacket *p2 = p->duplicate();
      click_ip6 *ip6 = reinterpret_cast<click_ip6*>(p2->data());
      ip6->ip6_dst = nxt;
      click_bier *bier_dup = reinterpret_cast<click_bier*>(p2->data()+sizeof(click_ip6));
      memcpy(bier_dup->bitstring, (bs & fbm).data_words(), bsl/(sizeof(Bitvector::word_type)*8));
      bier_dup->encode();
      // TODO: Add ethernet header to the packet
      
      // ifname is ensured to be in _ifaces because of the check upon route addition.
      output(_ifaces[ifname]).push(p2->duplicate());

      // RFC8279 Section 6.5 Step 8
      bs &= ~fbm;
    } else {
      click_chatter("Nexthop for BFR-ID %u not found in BIFT. Ignoring.", bfr_id);
      // Skip BFR
      bs[k] = 0;
    }
  }

  return -1;

  drop:
    drop(p_in);
    return -1;
}

int LookupBierTable::add_route(bfrid dst, bitstring fbm, IP6Address nxt, int output, String ifname, ErrorHandler *errh) {
  // if (output < 0 || output >= noutputs())
    // return errh->error("port number <%u> out of range", output);
  // TODO: Check if ifname is in table
  _t.add(dst, fbm, nxt, output, ifname);
  return 0;
}

void LookupBierTable::add_handlers() {
  add_write_handler("add", add_route_handler, 0);
  // TODO: implement route deletion
  add_read_handler("table", table_handler, 0);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(BierRouteTable)
EXPORT_ELEMENT(LookupBierTable)
