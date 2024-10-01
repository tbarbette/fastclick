/*
 * ip6drop.{cc,hh} -- element drops packets following a Gilbert Eliott model
 * Louis Navarre
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
#include "ip6drop.hh"
#include <click/nameinfo.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>

CLICK_DECLS

IP6Drop::IP6Drop()
{

}

IP6Drop::~IP6Drop()
{
    Stats &s = *_stats;
    click_chatter("Total number of dropped packets: %u\n", s.total_drop);
}

int
IP6Drop::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_all("ADDR", addrs)
    .read_or_set("P", p, 0)
    .read_or_set("R", r, 0)
    .read_or_set("H", h, 0)
    .read_or_set("K", k, 1)
    .read_or_set("UNIFORM", is_uniform, false)
    .read_or_set("UDROPRATE", uniform_drop, 0.03)
    .read_or_set("DETERMINISTIC", is_deterministic, false)
    .read_or_set("SEED", seed, 51)
	.complete() < 0)
        return -1;


    srand(seed);

    if (is_deterministic)
    {
        click_chatter("Uses determistic losses with %f burst losses", uniform_drop);
        if (uniform_drop > 0 && uniform_drop < 0.01)
            return errh->error("Determenistic drop is limited to a decimal % precision");
    }
    else
    {
        if (is_uniform) {
            click_chatter("Uses uniformis_uniform drop with %f", uniform_drop);
        } else {
            click_chatter("Uses GE model with k=%.4f h=%.4f r=%.4f p=%.4f", k, h, r, p);
        }
    }

    return 0;
}

void
IP6Drop::push(int input, Packet *p_in)
{
    Packet* p = drop_model(p_in);
    if (p)
        output(0).push(p);
}

#if HAVE_BATCH
void
IP6Drop::push_batch(int input, PacketBatch *batch) {
    EXECUTE_FOR_EACH_PACKET_DROPPABLE(drop_model, batch, [](Packet *){});
    if (batch) {
        output_push_batch(0, batch);
    }
}
#endif

Packet *
IP6Drop::drop_model(Packet *p_in)
{
    Stats &s = *_stats;

    s.total_received++;
    s.total_seen++;
    if ((is_deterministic && deterministic_drop()) || (!is_deterministic && ((is_uniform && !uniform_model()) || (!is_uniform && !gemodel())))) {
        // const click_ip6_sr *srv6 = reinterpret_cast<const click_ip6_sr *>(p_in->data() + 14 + sizeof(click_ip6));
        // if (srv6->ip6_hdrlen == 7)
        // {
        //     ++s.total_drop_source;
        // }
        p_in->kill();
        s.total_drop++;
        //click_chatter("Drop packet #%u", total_seen-1);
        return 0;
    }
    return p_in;
}

bool
IP6Drop::gemodel()
{
    _lock.acquire();
    auto &s = _protected;
    bool keep_packet = true;
    bool change_state = false;
    // click_chatter("State is %u", state);
    if (s.state == good) {
        double rand_val1 = rand() / (RAND_MAX + 1.);
        // click_chatter("Generated value: %f", rand_val1);
        keep_packet = rand_val1 < k;
        rand_val1 = rand() / (RAND_MAX + 1.);
        change_state = rand_val1 < p;
        // click_chatter("K and keep packet: %f, %u change state=%u (p=%u)", k * 100, keep_packet, change_state, p * 100);
        if (change_state) {
            s.state = bad;
        }
    } else {
        double rand_val1 = rand() / (RAND_MAX + 1.);
        keep_packet = rand_val1 < h;
        rand_val1 = rand() / (RAND_MAX + 1.);
        change_state = rand_val1 < r;
	// change_state = nb_burst >= 4;
	if (change_state) {
            s.state = good;
        } else {
	}
    }
    _lock.release();
    return keep_packet;
}

bool
IP6Drop::deterministic_drop()
{
    _lock.acquire();
    auto &p = _protected;
    p.de_count++;
    if ( p.de_count % 100 < uniform_drop * 100)
    {
            _lock.release();
        return true;
    }
        _lock.release();
    return false;
}

bool
IP6Drop::uniform_model()
{
    double randval = rand() / (RAND_MAX + 1.);
    return randval > uniform_drop;
}

bool
IP6Drop::addr_eq(uint32_t *a1, uint32_t *a2)
{
    return (a1[0] == a2[0] && a1[1] == a2[1] && a1[2] == a2[2] && a1[3] == a2[3]);
}

String
IP6Drop::read_handler(Element *e, void *thunk)
{
    IP6Drop *d = (IP6Drop *)e;
    switch((uintptr_t)thunk) {
        case 0: {
            PER_THREAD_MEMBER_SUM(uint64_t,total_drop,d->_stats,total_drop);
            return String(total_drop); }
        case 1: {
            PER_THREAD_MEMBER_SUM(uint64_t,total_received,d->_stats,total_received);
            return String(total_received); }
        case 2: {
            PER_THREAD_MEMBER_SUM(uint64_t,total_drop_source,d->_stats,total_drop_source);
                return String(total_drop_source); }
    }

    return "<error>";
}

void
IP6Drop::add_handlers()
{
    add_read_handler("drops", read_handler, 0);
    add_read_handler("count", read_handler, 1);
    add_read_handler("drop_source", read_handler, 2);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6Drop)
ELEMENT_MT_SAFE(IP6Drop)
