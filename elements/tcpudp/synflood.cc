/*
 * synflood.{cc,hh} -- generates TCP SYN flood
 * Massimo Girondi
 *
 * Copyright (c) 2021 KTH Royal Institute of Technology
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

#include "synflood.hh"
#include <click/handlercall.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/standard/scheduleinfo.hh>


CLICK_DECLS

SYNFlood::SYNFlood() : _task(this) {
#if HAVE_BATCH
    in_batch_mode = BATCH_MODE_YES;
#endif
}

SYNFlood::~SYNFlood() {}

int SYNFlood::configure(Vector<String> &conf, ErrorHandler *errh) {
    bool stop = false, active = true;
    unsigned burst = 1;

    if (Args(conf, this, errh)
            .read_mp("SRCIP", _sipaddr)
            .read_mp("DSTIP", _dipaddr)
            .read_or_set("SPORT", _sport, 1)
            .read_or_set("DPORT", _dport, 80)
            .read_or_set("STOP", stop, 1)
            .read_or_set("ACTIVE", active, 1)
            .read_or_set("BURST", burst, 32)
            .read_or_set("LIMIT", _limit, -1)
            .read_or_set("LEN", _len, 60)
            .complete() < 0)
        return -1;

    _stop = stop;
    _active = active;
    _burst = burst;

    if (_len < 60) {
        click_chatter("warning: packet length < 60, defaulting to 60");
        _len = 60;
    }

    return 0;
}

int SYNFlood::initialize(ErrorHandler *errh) {
    if (output_is_push(0))
        ScheduleInfo::initialize_task(this, &_task, _active, errh);
    else
        _notifier.initialize(Notifier::EMPTY_NOTIFIER, router());

    return 0;
}

inline Packet *SYNFlood::get_packet(bool push) {
    WritablePacket *p = Packet::make(_len);
    click_ip *iph = reinterpret_cast<click_ip *>(p->data() + 14);
    click_tcp *tcph = reinterpret_cast<click_tcp *>(iph + 1);
    p->set_mac_header(p->data());
    
    // set up IP header
    iph->ip_v = 4;
    iph->ip_hl = sizeof(click_ip) >> 2;
    iph->ip_len = htons(_len - 14);
    iph->ip_id = 0;
    iph->ip_p = IP_PROTO_TCP;
    iph->ip_src = _sipaddr;
    iph->ip_dst = _dipaddr;
    iph->ip_tos = 0;
    iph->ip_off = 0;
    iph->ip_ttl = 250;
    iph->ip_sum = 0;
    // No Checksum, we let the card compute it for us
    // (or the click element)
    //_iph->ip_sum = click_in_cksum((unsigned char *)ip, sizeof(click_ip));

    // set up TCP header
    tcph->th_sport = htons(_sport);
    tcph->th_dport = htons(_dport);
    tcph->th_seq = click_random();
    tcph->th_ack = click_random();
    tcph->th_off = sizeof(click_tcp) >> 2;
    tcph->th_flags = TH_SYN;
    tcph->th_win = 65535;
    tcph->th_urp = 0;
    tcph->th_sum = 0;

    _sport++;
    if (unlikely(_sport == 0)) {
        _sport++;
        //_sipaddr.s_addr = (_sipaddr.s_addr + 1);

        // The IP are in network byte order.
        _sipaddr.s_addr = htonl(htonl(_sipaddr.s_addr) + 1);
    }

    if (likely(_limit > 0)) _limit--;
    return p;
}

void SYNFlood::run_timer(Timer *) {
    if (_active) {
        if (output_is_pull(0)) {
            _notifier.wake();
        } else
            _task.reschedule();
    }
}

bool SYNFlood::run_task(Task *) {
    if (!_active) return false;

#if HAVE_BATCH
    PacketBatch *head = NULL;
    Packet *last = NULL;
    Packet *p = NULL;
    for (int i = 0; i < _burst; i++) {
        p = get_packet(1);
        if (likely(p)) {
            if (unlikely(head == NULL)) {
                head = PacketBatch::start_head(p);
                last = p;
            } else {
                last->set_next(p);
                last = last->next();
            }
        } else
            break;
    }
    if (likely(head)) output_push_batch(0, head->make_tail(last, _burst));
#else
    for (int i = 0; i < _burst; i++) {
        Packet *p = get_packet(1);
        output(0).push(p);
    }
#endif

    if (_active) {
        if (likely(_limit)) {
            _task.fast_reschedule();
        } else {
            click_chatter("Generation finished. Asking the driver to stop.");
            router()->please_stop_driver();
        }
    }

    return true;
}

Packet *SYNFlood::pull(int) {
    if (!_active) return 0;
    Packet *p = get_packet();
    if (p)
        _notifier.wake();
    else if (_stop)
        router()->please_stop_driver();
    return p;
}
#if HAVE_BATCH
PacketBatch *SYNFlood::pull_batch(int, unsigned max) {
    if (!_active) return 0;
    PacketBatch *batch = 0;
    MAKE_BATCH(get_packet(), batch, max);
    if (!batch) {
        if (_stop) router()->please_stop_driver();
        return 0;
    }
    if (batch->count() == max) {
        _notifier.wake();
    }
    return batch;
}
#endif

enum { H_ACTIVE, H_STOP };

String SYNFlood::read_handler(Element *e, void *thunk) {
    SYNFlood *fd = static_cast<SYNFlood *>(e);
    switch ((intptr_t)thunk) {
    case H_ACTIVE:
        return BoolArg::unparse(fd->_active);
    default:
        return "<error>";
    }
}

int SYNFlood::write_handler(const String &s_in, Element *e, void *thunk,
                            ErrorHandler *errh) {
    SYNFlood *fd = static_cast<SYNFlood *>(e);
    String s = cp_uncomment(s_in);
    switch ((intptr_t)thunk) {
    case H_ACTIVE: {
        bool active;
        if (BoolArg().parse(s, active)) {
            fd->_active = active;
            if (fd->output_is_push(0) && active && !fd->_task.scheduled())
                fd->_task.reschedule();
            else if (!fd->output_is_push(0))
                fd->_notifier.set_active(active, true);
            return 0;
        } else
            return errh->error("type mismatch");
    }
    case H_STOP:
        fd->_active = false;
        fd->router()->please_stop_driver();
        return 0;
    default:
        return -EINVAL;
    }
}

void SYNFlood::add_handlers() {
    add_read_handler("active", read_handler, H_ACTIVE, Handler::f_checkbox);
    add_write_handler("active", write_handler, H_ACTIVE);
    add_write_handler("stop", write_handler, H_STOP, Handler::f_button);
    if (output_is_push(0)) add_task_handlers(&_task);
}

ELEMENT_REQUIRES(userlevel dpdk)
EXPORT_ELEMENT(SYNFlood)
CLICK_ENDDECLS
