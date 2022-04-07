/*
 * FastUDPFlowsIP6.{cc,hh} -- fast udp flow source, a benchmark tool
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
#include "fastudpflowsip6.hh"
#include <click/args.hh>
#include <click/etheraddress.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/glue.hh>
#include <click/router.hh>
#include <click/standard/alignmentinfo.hh>
#include <click/standard/scheduleinfo.hh>


CLICK_DECLS

const unsigned FastUDPFlowsIP6::NO_LIMIT;

FastUDPFlowsIP6::FastUDPFlowsIP6()
  : _flows(0), _task(this), _timer(this)
{
#if HAVE_BATCH
    in_batch_mode = BATCH_MODE_YES;
#endif
    _rate_limited = true;
    _first = _last = 0;
    _count = 0;
    _stop = false;
    _sequential = false;
}

FastUDPFlowsIP6::~FastUDPFlowsIP6()
{
}

int
FastUDPFlowsIP6::configure(Vector<String> &conf, ErrorHandler *errh)
{
    unsigned rate;
    int limit;
    int len;
    if (Args(conf, this, errh)
        .read_mp("RATE", rate)
        .read_mp("LIMIT", limit)
        .read_mp("LENGTH", len)
        .read_mp("SRCETH", EtherAddressArg(), _ethh.ether_shost)
        .read_mp("SRCIP6", _sip6addr)
        .read_mp("DSTETH", EtherAddressArg(), _ethh.ether_dhost)
        .read_mp("DSTIP6", _dip6addr)
        .read_mp("FLOWS", _nflows)
        .read_mp("FLOWSIZE", _flowsize)
        .read_or_set("FLOWBURST", _flowburst, 1)
        .read_or_set("CHECKSUM", _cksum, true)
        .read_or_set("SEQUENTIAL", _sequential, false)
        .read_or_set("ACTIVE", _active, true)
        .read_or_set("STOP", _stop, false)
        .complete() < 0)
        return -1;

    set_length(len);
    _ethh.ether_type = htons(0x86DD);
    if(rate != 0){
        _rate_limited = true;
        _rate.set_rate(rate, errh);
    } else {
        _rate_limited = false;
    }

    _limit = (limit >= 0 ? limit : NO_LIMIT);

    return 0;
}

void
FastUDPFlowsIP6::change_ports(int flow)
{
    WritablePacket *q = _flows[flow].packet->uniqueify(); // better not fail
    _flows[flow].packet = q;
    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(q->data()+14);
    click_udp *udp = reinterpret_cast<click_udp *>(ip6 + 1);

    udp->uh_sport = (_sequential?udp->uh_sport+1:click_random() >> 2) % 0xFFFF;
    udp->uh_dport = (_sequential?udp->uh_dport+1:click_random() >> 2) % 0xFFFF;
    udp->uh_sum = 0;
    unsigned short len = _len-14-sizeof(click_ip);
    if (_cksum) {
        udp->uh_sum = htons(in6_fast_cksum(&ip6->ip6_src, &ip6->ip6_dst, ip6->ip6_plen, ip6->ip6_nxt, udp->uh_sum, (unsigned char *)udp, ip6->ip6_plen));

    } else
        udp->uh_sum = 0;
}

Packet *
FastUDPFlowsIP6::get_packet()
{
    int flow;
    if (_last_flow->burst_count++ < _flowburst) {
        flow = _last_flow->index % _nflows;
    } else {
        flow = (_sequential?(_last_flow->index)++ : (click_random() >> 2)) % _nflows;
        _last_flow->burst_count = 1;
    }

    if (_flows[flow].flow_count >= _flowsize) {
        change_ports(flow);
        _flows[flow].flow_count = 0;
    }
    _flows[flow].flow_count++;

    return _flows[flow].packet->clone();
}

int
FastUDPFlowsIP6::initialize(ErrorHandler * errh)
{
    _count = 0;
    _flows = new flow_t[_nflows];

    for (unsigned i=0; i<_nflows; i++) {
        WritablePacket *q = Packet::make(_len);
        if (unlikely(!q)) {
            return errh->error("Could not initialize packet, out of memory?");
        }
        _flows[i].packet = q;
        memcpy(q->data(), &_ethh, 14);
        click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(q->data()+14);
        click_udp *udp = reinterpret_cast<click_udp *>(ip6 + 1);

        // set up IP6 header
        ip6->ip6_flow = 0;
        ip6->ip6_v = 6;
        ip6->ip6_plen = htons(_len - 14 - sizeof(click_ip6));
        ip6->ip6_nxt = IP_PROTO_UDP;
        ip6->ip6_hlim = 250;
        ip6->ip6_src = _sip6addr;
        ip6->ip6_dst = _dip6addr;
        SET_DST_IP6_ANNO(q, _dip6addr);
        q->set_ip6_header(ip6, sizeof(click_ip6));

        // set up UDP header
        udp->uh_sport = (click_random() >> 2) % 0xFFFF;
        udp->uh_dport = (click_random() >> 2) % 0xFFFF;
        udp->uh_sum = 0;
        unsigned short len = _len-14-sizeof(click_ip6);
        udp->uh_ulen = htons(len);
        if (_cksum) {
            //need to change, use our own checksum method
            //unsigned csum = ~click_in_cksum((unsigned char *)udp, len) & 0xFFFF;
            //udp->uh_sum = csum_tcpudp_magic(_sipaddr.s_addr, _dipaddr.s_addr,
            //			    len, IP_PROTO_UDP, csum);
            udp->uh_sum = htons(in6_fast_cksum(&ip6->ip6_src, &ip6->ip6_dst, ip6->ip6_plen, ip6->ip6_nxt, udp->uh_sum, (unsigned char *)udp, ip6->ip6_plen));

        } else
            udp->uh_sum = 0;

        _flows[i].flow_count = 0;
    }

    if (output_is_push(0)) {
        ScheduleInfo::initialize_task(this, &_task, true, errh);
        _timer.initialize(this);
    }

    return 0;
}

void
FastUDPFlowsIP6::run_timer(Timer*) {
    _task.reschedule();
}


bool
FastUDPFlowsIP6::run_task(Task* t) {
#if HAVE_BATCH
    if (in_batch_mode) {
        const unsigned int max = 32;
        PacketBatch *batch;
        MAKE_BATCH(FastUDPFlowsIP6::pull(0), batch, max);
        if (likely(batch)) {
            output(0).push_batch(batch);
            t->fast_reschedule();
            return true;
        }
    } else
#endif
    {
        Packet* p = FastUDPFlowsIP6::pull(0);
        if (likely(p)) {
            output(0).push(p);
            t->fast_reschedule();
            return true;
        }
    }

    // We had no packet, we must set timer
    if (_rate_limited)
        _timer.schedule_at(_rate.expiry());
    return false;
}

void
FastUDPFlowsIP6::cleanup_flows() {
    if (_flows) {
        for (unsigned i=0; i<_nflows; i++) {
            if (_flows[i].packet)
                _flows[i].packet->kill();
            _flows[i].packet=0;
        }
        delete[] _flows;
        _flows = 0;
    }
}

void
FastUDPFlowsIP6::cleanup(CleanupStage)
{
    cleanup_flows();
}

Packet *
FastUDPFlowsIP6::pull(int)
{
    Packet *p = 0;

    if (!_active || (_limit != NO_LIMIT && _count >= _limit)) {
        return 0;
    }

    if(_rate_limited){
        if (_rate.need_update(Timestamp::now())) {
            _rate.update();
            p = get_packet();
        }
    } else {
        p = get_packet();
    }

    if (p) {
        _count++;
        if(_count == 1) {
            _first = click_jiffies();
        }
        if(_limit != NO_LIMIT && _count >= _limit) {
            _last = click_jiffies();
            if (_stop) {
                router()->please_stop_driver();
            }
        }
    }

    return p;
}

#if HAVE_BATCH
PacketBatch *
FastUDPFlowsIP6::pull_batch(int port, unsigned max) {
    PacketBatch *batch;
    MAKE_BATCH(FastUDPFlowsIP6::pull(port), batch, max);
    return batch;
}
#endif

void
FastUDPFlowsIP6::reset()
{
    _count = 0;
    _first = 0;
    _last = 0;
}

static String
FastUDPFlowsIP6_read_count_handler(Element *e, void *)
{
    FastUDPFlowsIP6 *c = (FastUDPFlowsIP6 *)e;
    return String(c->count());
}

static String
FastUDPFlowsIP6_read_rate_handler(Element *e, void *)
{
    FastUDPFlowsIP6 *c = (FastUDPFlowsIP6 *)e;
    if(c->last() != 0) {
        int d = c->last() - c->first();
        if (d < 1) {
            d = 1;
        }
        int rate = c->count() * CLICK_HZ / d;
        return String(rate);
    } else {
        return String("0");
    }
}

static int
FastUDPFlowsIP6_reset_write_handler
(const String &, Element *e, void *, ErrorHandler *)
{
    FastUDPFlowsIP6 *c = (FastUDPFlowsIP6 *)e;
    c->reset();
    return 0;
}

static int
FastUDPFlowsIP6_limit_write_handler
(const String &s, Element *e, void *, ErrorHandler *errh)
{
    FastUDPFlowsIP6 *c = (FastUDPFlowsIP6 *)e;
    int limit;
    if (!IntArg().parse(s, limit))
        return errh->error("limit parameter must be integer >= 0");
    c->_limit = (limit >= 0 ? limit : c->NO_LIMIT);
    return 0;
}

static int
FastUDPFlowsIP6_rate_write_handler
(const String &s, Element *e, void *, ErrorHandler *errh)
{
    FastUDPFlowsIP6 *c = (FastUDPFlowsIP6 *)e;
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
FastUDPFlowsIP6_active_write_handler
(const String &s, Element *e, void *, ErrorHandler *errh)
{
    FastUDPFlowsIP6 *c = (FastUDPFlowsIP6 *)e;
    bool active;
    if (!BoolArg().parse(s, active))
        return errh->error("active parameter must be boolean");
    c->_active = active;
    if (active) c->reset();
        return 0;
}

int
FastUDPFlowsIP6::length_write_handler
(const String &s, Element *e, void *, ErrorHandler *errh)
{
    FastUDPFlowsIP6 *c = (FastUDPFlowsIP6 *)e;
    unsigned len;
    if (!IntArg().parse(s, len))
        return errh->error("length parameter must be integer");
    if (len != c->_len) {
        c->set_length(len);
        c->cleanup_flows();
        c->initialize(0);
    }
    return 0;
}

int
FastUDPFlowsIP6::eth_write_handler
(const String &s, Element *e, void * param, ErrorHandler *errh)
{
  FastUDPFlowsIP6 *c = (FastUDPFlowsIP6 *)e;
  EtherAddress eth;
  if (!EtherAddressArg().parse(s, eth))
    return errh->error("Invalid argument");
  if (reinterpret_cast<int*>(param) == 0) {
     memcpy(c->_ethh.ether_shost,eth.sdata(),6);
  } else {
     memcpy(c->_ethh.ether_dhost,eth.sdata(),6);
  }
  if (c->_flows) {
      for (unsigned i=0; i<c->_nflows; i++) {
          if (c->_flows[i].packet)
              memcpy(static_cast<WritablePacket*>(c->_flows[i].packet)->data(),c->_ethh.ether_dhost,12);
      }
  }
  return 0;
}

void
FastUDPFlowsIP6::add_handlers()
{
    add_read_handler("count", FastUDPFlowsIP6_read_count_handler, 0);
    add_read_handler("rate", FastUDPFlowsIP6_read_rate_handler, 0);
    add_write_handler("rate", FastUDPFlowsIP6_rate_write_handler, 0);
    add_write_handler("reset", FastUDPFlowsIP6_reset_write_handler, 0, Handler::BUTTON);
    add_write_handler("active", FastUDPFlowsIP6_active_write_handler, 0, Handler::CHECKBOX);
    add_write_handler("limit", FastUDPFlowsIP6_limit_write_handler, 0);
    add_write_handler("srceth", FastUDPFlowsIP6::eth_write_handler, 0);
    add_write_handler("dsteth", FastUDPFlowsIP6::eth_write_handler, 1);
    add_data_handlers("length", Handler::OP_READ, &_len);
    add_write_handler("length", length_write_handler, 0);
    add_data_handlers("stop", Handler::OP_READ | Handler::OP_WRITE, &_stop);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(ip6)
EXPORT_ELEMENT(FastUDPFlowsIP6)
