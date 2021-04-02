/*
 * aggcountervector.{cc,hh} -- count packets/bytes with given aggregate
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
#include "aggcountervector.hh"
#include <click/handlercall.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/packet_anno.hh>
#include <click/integers.hh>	// for first_bit_set
#include <click/router.hh>
CLICK_DECLS



AggregateCounterVector::AggregateCounterVector() : _epoch(0)
{
}


AggregateCounterVector::~AggregateCounterVector()
{
}


int
AggregateCounterVector::configure(Vector<String> &conf, ErrorHandler *errh)
{
    bool bytes = false;
    bool ip_bytes = false;
    bool packet_count = true;
    bool extra_length = true;
    uint32_t freeze_nnz, stop_nnz;
    uint32_t mask = (uint32_t)-1;
    uint64_t freeze_count, stop_count;
    bool mark = false;
    String call_nnz, call_count;

    if (Args(conf, this, errh)
    .read_mp("MASK", mask)
    .read("MARK", mark)
	.read("BYTES", bytes)
	.read("IP_BYTES", ip_bytes)
	.read("MULTIPACKET", packet_count)
	.read("EXTRA_LENGTH", extra_length)
	.complete() < 0)
	return -1;

    _bytes = bytes;
    _ip_bytes = ip_bytes;
    _use_packet_count = packet_count;
    _use_extra_length = extra_length;
    _mask = mask;
    _mark = mark;

    _nodes.resize(mask + 1);


    return 0;
}

int
AggregateCounterVector::initialize(ErrorHandler *errh)
{
    _active = true;
    return 0;
}

void
AggregateCounterVector::cleanup(CleanupStage)
{

}




inline bool
AggregateCounterVector::update_batch(PacketBatch *batch)
{
    if (!_active)
	return false;

    uint32_t last_agg = 0;
    Node *n = 0;

    FOR_EACH_PACKET(batch,p) {
        // AGGREGATE_ANNO is already in host byte order!
        uint32_t agg = AGGREGATE_ANNO(p) & _mask;
        if (agg == last_agg && n) {

        } else {
            bool outdated =false;
            n = &find_node(last_agg,p,outdated);
            if (outdated)
                return true;
            if (!n)
                return false;
            last_agg = agg;
        }

        uint32_t amount;
        if (!_bytes)
            amount = 1 + (_use_packet_count ? EXTRA_PACKETS_ANNO(p) : 0);
        else {
            amount = p->length() + (_use_extra_length ? EXTRA_LENGTH_ANNO(p) : 0);
        if (_ip_bytes && p->has_network_header())
            amount -= p->network_header_offset();
        }

        n->count += amount;
#if COUNT_FLOWS
        n->add_flow(AGGREGATE_ANNO(p));
#endif
    }
    return true;
}


inline bool
AggregateCounterVector::update(Packet *p)
{
    if (!_active)
	return false;


    // AGGREGATE_ANNO is already in host byte order!
    uint32_t agg = AGGREGATE_ANNO(p) & _mask;
    bool outdated = false;
    Node &n = find_node(agg, p, outdated);

    if (outdated)
        return true;
    uint32_t amount;
    if (!_bytes)
	amount = 1 + (_use_packet_count ? EXTRA_PACKETS_ANNO(p) : 0);
    else {
	amount = p->length() + (_use_extra_length ? EXTRA_LENGTH_ANNO(p) : 0);
	if (_ip_bytes && p->has_network_header())
	    amount -= p->network_header_offset();
    }
#if COUNT_FLOWS
    n.add_flow(AGGREGATE_ANNO(p));
#endif
    n.count += amount;

    return true;
}

void
AggregateCounterVector::push(int port, Packet *p)
{
    port = !update(p);
    output(noutputs() == 1 ? 0 : port).push(p);
}

Packet *
AggregateCounterVector::pull(int)
{
    Packet *p = input(0).pull();
    if (p && _active)
	update(p);
    return p;
}

#if HAVE_BATCH
void
AggregateCounterVector::push_batch(int port, PacketBatch *batch)
{
    auto fnt = [this,port](Packet*p){return !update(p);};
    CLASSIFY_EACH_PACKET(2,fnt,batch,[this](int port, PacketBatch* batch){ output(0).push_batch(batch);});
}

PacketBatch *
AggregateCounterVector::pull_batch(int port,unsigned max)
{
    PacketBatch *batch = input(0).pull_batch(max);
    if (batch && _active) {
        FOR_EACH_PACKET(batch,p) {
		update(p);
        }
    }
    return batch;
}
#endif


enum {
    AC_ACTIVE, AC_STOP,
};

String
AggregateCounterVector::read_handler(Element *e, void *thunk)
{
    AggregateCounterVector *ac = static_cast<AggregateCounterVector *>(e);
    switch ((intptr_t)thunk) {
      default:
	return "<error>";
    }
}

int
AggregateCounterVector::write_handler(const String &data, Element *e, void *thunk, ErrorHandler *errh)
{
    AggregateCounterVector *ac = static_cast<AggregateCounterVector *>(e);
    String s = cp_uncomment(data);
    switch ((intptr_t)thunk) {
      case AC_ACTIVE: {
	  bool val;
	  if (!BoolArg().parse(s, val))
	      return errh->error("type mismatch");
	  ac->_active = val;
	  return 0;
      }
      case AC_STOP:
	ac->_active = false;
	ac->router()->please_stop_driver();
	return 0;
      default:
	return errh->error("internal error");
    }
}


void
AggregateCounterVector::add_handlers()
{
    add_data_handlers("active", Handler::f_read | Handler::f_checkbox, &_active);
    add_write_handler("active", write_handler, AC_ACTIVE);
    add_write_handler("stop", write_handler, AC_STOP, Handler::f_button);
}


ELEMENT_REQUIRES(userlevel int64)
EXPORT_ELEMENT(AggregateCounterVector)
ELEMENT_MT_SAFE(AggregateCounterVector)
CLICK_ENDDECLS
