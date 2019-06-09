/*
 * fasttcpflows.{cc,hh} -- fast tcp flow source, a benchmark tool
 * Benjie Chen
 *
 * Computational batching support
 * by Georgios Katsikas
 *
 * Copyright (c) 1999-2001 Massachusetts Institute of Technology
 * Copyright (c) 2017 KTH Royal Institute of Technology
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
#include "fasttcpflows.hh"
#include <click/args.hh>
#include <click/etheraddress.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/standard/alignmentinfo.hh>

CLICK_DECLS

const unsigned FastTCPFlows::NO_LIMIT;

FastTCPFlows::FastTCPFlows()
  : _flows(0), _end_h{nullptr}
{
#if HAVE_BATCH
  in_batch_mode = BATCH_MODE_YES;
#endif
  _rate_limited = true;
  _first = _last = 0;
  _count = 0;
}

FastTCPFlows::~FastTCPFlows()
{
}

int
FastTCPFlows::configure(Vector<String> &conf, ErrorHandler *errh)
{
  _cksum = true;
  _active = true;
  unsigned rate;
  int limit;
  bool stop = false;
  if (Args(conf, this, errh)
      .read_mp("RATE", rate)
      .read_mp("LIMIT", limit)
      .read_mp("LENGTH", _len)
      .read_mp("SRCETH", EtherAddressArg(), _ethh.ether_shost)
      .read_mp("SRCIP", _sipaddr)
      .read_mp("DSTETH", EtherAddressArg(), _ethh.ether_dhost)
      .read_mp("DSTIP", _dipaddr)
      .read_mp("FLOWS", _nflows)
      .read_mp("FLOWSIZE", _flowsize)
      .read_p("ACTIVE", _active)
      .read("STOP", stop)
      .complete() < 0)
    return -1;
  if (_flowsize < 3) {
    click_chatter("warning: flow size < 3, defaulting to 3");
    _flowsize = 3;
  }
  if (_len < 60) {
    click_chatter("warning: packet length < 60, defaulting to 60");
    _len = 60;
  }
  _ethh.ether_type = htons(0x0800);
  if(rate != 0){
    _rate_limited = true;
    _rate.set_rate(rate, errh);
  } else {
    _rate_limited = false;
  }
  _limit = (limit >= 0 ? limit : NO_LIMIT);
  if (stop) {
    _end_h = new HandlerCall("stop");
  }
  return 0;
}

void
FastTCPFlows::change_ports(int flow)
{
  unsigned short sport = (click_random() >> 2) % 0xFFFF;
  unsigned short dport = (click_random() >> 2) % 0xFFFF;
  WritablePacket *q = _flows[flow].syn_packet->uniqueify(); // better not fail
  _flows[flow].syn_packet = q;
  click_ip *ip =
    reinterpret_cast<click_ip *>(q->data()+14);
  click_tcp *tcp = reinterpret_cast<click_tcp *>(ip + 1);
  tcp->th_sport = sport;
  tcp->th_dport = dport;
  tcp->th_sum = 0;
  unsigned short len = _len-14-sizeof(click_ip);
  unsigned csum = click_in_cksum((uint8_t *)tcp, len);
  tcp->th_sum = click_in_cksum_pseudohdr(csum, ip, len);

  q = _flows[flow].data_packet->uniqueify(); // better not fail
  _flows[flow].data_packet = q;
  ip = reinterpret_cast<click_ip *>(q->data()+14);
  tcp = reinterpret_cast<click_tcp *>(ip + 1);
  tcp->th_sport = sport;
  tcp->th_dport = dport;
  tcp->th_sum = 0;
  len = _len-14-sizeof(click_ip);
  csum = click_in_cksum((uint8_t *)tcp, len);
  tcp->th_sum = click_in_cksum_pseudohdr(csum, ip, len);

  q = _flows[flow].fin_packet->uniqueify(); // better not fail
  _flows[flow].fin_packet = q;
  ip = reinterpret_cast<click_ip *>(q->data()+14);
  tcp = reinterpret_cast<click_tcp *>(ip + 1);
  tcp->th_sport = sport;
  tcp->th_dport = dport;
  tcp->th_sum = 0;
  len = _len-14-sizeof(click_ip);
  csum = click_in_cksum((uint8_t *)tcp, len);
  tcp->th_sum = click_in_cksum_pseudohdr(csum, ip, len);
}

Packet *
FastTCPFlows::get_packet()
{
  if (_limit != NO_LIMIT && _count >= _limit) {
    for (unsigned i=0; i<_nflows; i++) {
      if (_flows[i].flow_count != _flowsize) {
	_flows[i].flow_count = _flowsize;
	return _flows[i].fin_packet->clone();
      }
    }
    _sent_all_fins = true;
    return 0;
  }
  else {
    int flow = (click_random() >> 2) % _nflows;
    if (_flows[flow].flow_count == _flowsize) {
      change_ports(flow);
      _flows[flow].flow_count = 0;
    }
    _flows[flow].flow_count++;
    if (_flows[flow].flow_count == 1) {
      return _flows[flow].syn_packet->clone();
    } else if (_flows[flow].flow_count == _flowsize) {
      return _flows[flow].fin_packet->clone();
    } else {
      return _flows[flow].data_packet->clone();
    }
  }
}

Packet *FastTCPFlows::make_packet(
    unsigned len, click_ether eth, in_addr sipaddr, in_addr dipaddr,
    unsigned short sport, unsigned short dport, uint8_t flags) {

  WritablePacket *q = Packet::make(len);
  memcpy((void*)q->data(), &eth, 14);
  click_ip *ip = reinterpret_cast<click_ip *>(q->data()+14);
  click_tcp *tcp = reinterpret_cast<click_tcp *>(ip + 1);
  // set up IP header
  ip->ip_v = 4;
  ip->ip_hl = sizeof(click_ip) >> 2;
  ip->ip_len = htons(len-14);
  ip->ip_id = 0;
  ip->ip_p = IP_PROTO_TCP;
  ip->ip_src = sipaddr;
  ip->ip_dst = dipaddr;
  ip->ip_tos = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 250;
  ip->ip_sum = 0;
  ip->ip_sum = click_in_cksum((unsigned char *)ip, sizeof(click_ip));
  q->set_dst_ip_anno(IPAddress(dipaddr));
  q->set_ip_header(ip, sizeof(click_ip));
  // set up TCP header
  tcp->th_sport = sport;
  tcp->th_dport = dport;
  tcp->th_seq = click_random();
  tcp->th_ack = click_random();
  tcp->th_off = sizeof(click_tcp) >> 2;
  tcp->th_flags = flags;
  tcp->th_win = 65535;
  tcp->th_urp = 0;
  tcp->th_sum = 0;
  unsigned short len_ = len-14-sizeof(click_ip);
  unsigned csum = click_in_cksum((uint8_t *)tcp, len_);
  tcp->th_sum = click_in_cksum_pseudohdr(csum, ip, len_);

  return q;

}

int
FastTCPFlows::initialize(ErrorHandler *errh)
{
  _count = 0;
  _sent_all_fins = false;
  _flows = new flow_t[_nflows];

  if (_end_h && _end_h->initialize_write(this, errh) < 0)
      return -1;

  for (unsigned i=0; i<_nflows; i++) {
    unsigned short sport = (click_random() >> 2) % 0xFFFF;
    unsigned short dport = (click_random() >> 2) % 0xFFFF;

    // SYN packet
    _flows[i].syn_packet = make_packet(
        _len, _ethh, _sipaddr, _dipaddr, sport, dport, TH_SYN);

    // DATA packet with PUSH and ACK
    _flows[i].data_packet = make_packet(
        _len, _ethh, _sipaddr, _dipaddr, sport, dport, TH_PUSH | TH_ACK);

    // FIN packet
    _flows[i].fin_packet = make_packet(
        _len, _ethh, _sipaddr, _dipaddr, sport, dport, TH_FIN);

    _flows[i].flow_count = 0;
  }
  _last_flow = 0;
  return 0;
}

void
FastTCPFlows::cleanup(CleanupStage)
{
  if (_flows) {
    for (unsigned i=0; i<_nflows; i++) {
      _flows[i].syn_packet->kill();
      _flows[i].data_packet->kill();
      _flows[i].fin_packet->kill();
    }
    delete[] _flows;
    _flows = 0;
  }
}

Packet *
FastTCPFlows::pull(int)
{
  Packet *p = 0;

  if (!_active)
    return 0;

  if (_limit != NO_LIMIT && _count >= _limit && _sent_all_fins) {
    if (_end_h) {
      _end_h->call_write();
    }
    return 0;
  }

  if(_rate_limited){
    if (_rate.need_update(Timestamp::now())) {
      _rate.update();
      p = get_packet();
    }
  } else
    p = get_packet();

  if(p) {
    _count++;
    if(_count == 1)
      _first = click_jiffies();
    if(_limit != NO_LIMIT && _count >= _limit)
      _last = click_jiffies();
  }

  return(p);
}

#if HAVE_BATCH
PacketBatch *
FastTCPFlows::pull_batch(int port, unsigned max) {
      PacketBatch *batch;
      MAKE_BATCH(FastTCPFlows::pull(port), batch, max);
      return batch;
}
#endif

void
FastTCPFlows::reset()
{
  _count = 0;
  _first = 0;
  _last = 0;
  _sent_all_fins = false;
}

static String
FastTCPFlows_read_count_handler(Element *e, void *)
{
  FastTCPFlows *c = (FastTCPFlows *)e;
  return String(c->count());
}

static String
FastTCPFlows_read_rate_handler(Element *e, void *)
{
  FastTCPFlows *c = (FastTCPFlows *)e;
  if(c->last() != 0){
    int d = c->last() - c->first();
    if (d < 1) d = 1;
    int rate = c->count() * CLICK_HZ / d;
    return String(rate);
  } else {
    return String("0");
  }
}

static int
FastTCPFlows_reset_write_handler
(const String &, Element *e, void *, ErrorHandler *)
{
  FastTCPFlows *c = (FastTCPFlows *)e;
  c->reset();
  return 0;
}

static int
FastTCPFlows_limit_write_handler
(const String &s, Element *e, void *, ErrorHandler *errh)
{
  FastTCPFlows *c = (FastTCPFlows *)e;
  int limit;
  if (!IntArg().parse(s, limit))
    return errh->error("limit parameter must be integer >= 0");
  c->_limit = (limit >= 0 ? limit : c->NO_LIMIT);
  return 0;
}

static int
FastTCPFlows_rate_write_handler
(const String &s, Element *e, void *, ErrorHandler *errh)
{
  FastTCPFlows *c = (FastTCPFlows *)e;
  unsigned rate;
  if (!IntArg().parse(s, rate))
    return errh->error("rate parameter must be integer >= 0");
  if (rate > GapRate::MAX_RATE)
    // report error rather than pin to max
    return errh->error("rate too large; max is %u", GapRate::MAX_RATE);
  c->_rate.set_rate(rate);
  return 0;
}

static int
FastTCPFlows_active_write_handler
(const String &s, Element *e, void *, ErrorHandler *errh)
{
  FastTCPFlows *c = (FastTCPFlows *)e;
  bool active;
  if (!BoolArg().parse(s, active))
    return errh->error("active parameter must be boolean");
  c->_active = active;
  if (active) c->reset();
  return 0;
}

void
FastTCPFlows::add_handlers()
{
  add_read_handler("count", FastTCPFlows_read_count_handler, 0);
  add_read_handler("rate", FastTCPFlows_read_rate_handler, 0);
  add_write_handler("rate", FastTCPFlows_rate_write_handler, 0);
  add_write_handler("reset", FastTCPFlows_reset_write_handler, 0, Handler::BUTTON);
  add_write_handler("active", FastTCPFlows_active_write_handler, 0, Handler::CHECKBOX);
  add_write_handler("limit", FastTCPFlows_limit_write_handler, 0);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(FastTCPFlows)
