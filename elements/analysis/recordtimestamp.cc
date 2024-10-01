/*
 * recordtimestamp.{cc,hh} -- Store a timestamp for numbered packets
 * Cyril Soldani, Tom Barbette
 *
 * Copyright (c) 2015-2016 University of Liège
 * Copyright (c) 2019 KTH Royal Institute of Technology
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

#include <click/config.h> // Doc says this should come first

#include "recordtimestamp.hh"

#include <click/args.hh>
#include <click/error.hh>
#include <click/hashtable.hh>

#if HAVE_DPDK
#include "../elements/userlevel/todpdkdevice.hh"
#endif

CLICK_DECLS

RecordTimestamp::RecordTimestamp() :
    _offset(-1), _dynamic(false), _net_order(false), _timestamps(), _np(0) {
}

RecordTimestamp::~RecordTimestamp() {
}

#if HAVE_DPDK
inline uint16_t
RecordTimestamp::calc_latency(uint16_t port __rte_unused, uint16_t qidx,
                struct rte_mbuf **pkts, uint16_t nb_pkts, void *ptr)
{
    RecordTimestamp* rt = ((RecordTimestamp*)ptr);
    for (unsigned i = 0; i < nb_pkts; i++) {

        unsigned char *data = rte_pktmbuf_mtod(pkts[i], unsigned char *);
        uint64_t n = (*(reinterpret_cast<const uint64_t *>(data + rt->_offset)));
        if (rt->_sample > 1) {
            if (n % rt->_sample == 0)
                n = n / rt->_sample;
            else
                continue;
        }

    //click_chatter("Record qidx%d %p{element} -> %d",qidx, rt, n);
        rt->_timestamps[n] = TimestampT::now_steady();
    }
    return nb_pkts;
}
#endif

int RecordTimestamp::configure(Vector<String> &conf, ErrorHandler *errh) {
    uint32_t n = 0;
    Element *e = NULL;
#if HAVE_DPDK
    ToDPDKDevice* _tx_dev = 0;
    int _tx_dev_id = 0;
#endif
    if (Args(conf, this, errh)
            .read("COUNTER", e)
            .read("N", n)
#if HAVE_DPDK
            .read("TXDEV", ElementCastArg("ToDPDKDevice"), _tx_dev)
            .read("TXDEV_QID", _tx_dev_id)
#endif
            .read_or_set("OFFSET", _offset, -1)
            .read_or_set("DYNAMIC", _dynamic, false)
            .read_or_set("NET_ORDER", _net_order, false)
            .read_or_set("SAMPLE", _sample, false)
            .complete() < 0)
        return -1;

    if (n == 0)
        n = 65536;
    _timestamps.reserve(n);

    if (e && (_np = static_cast<NumberPacket *>(e->cast("NumberPacket"))) == 0)
        return errh->error("COUNTER must be a valid NumberPacket element");

    // Adhere to the settings of the counter element, bypassing the configuration
    if (_np) {
        _net_order = _np->has_net_order();
    }
#if HAVE_DPDK
    if (_tx_dev) {
        if (_dynamic || _offset < 0)
            return errh->error("TXDEV is only compatible with OFFSET given and non-dynamic mode");

        if (_net_order)
            return errh->error("TXDEV is only compatible with non-net order");

        _timestamps.resize(_timestamps.size() == 0? _timestamps.capacity():_timestamps.size() * 2, Timestamp::uninitialized_t());
                    DPDKDevice::all_initialized.post(new Router::FctFuture([this,_tx_dev,_tx_dev_id](ErrorHandler* errh) {
            if (rte_eth_add_tx_callback(_tx_dev->port_id(), _tx_dev_id, calc_latency, this) == 0) {
                return errh->error("Port %d/%d Callback could not be set %d: %s", _tx_dev->port_id(), _tx_dev_id, rte_errno, rte_strerror(rte_errno));
            }
            return 0;
            },this));
    } else
#endif

    if (ninputs() != 1 or noutputs() !=1)
        return errh->error("You need to pass either TXDEV or use the element in path.");

    return 0;
}

inline void
RecordTimestamp::rmaction(Packet *p) {
    uint64_t i;
    if (_offset >= 0) {
        i = get_numberpacket(p, _offset, _net_order);
        if (_sample > 1) {
            if (i % _sample == 0)
                i = i / _sample;
            else
                return;
        }
        while (unlikely(i >= (unsigned)_timestamps.size())) {
            if (!_dynamic && i >= (unsigned)_timestamps.capacity()) {
                click_chatter("Fatal error: DYNAMIC is not set and record timestamp reserved capacity is too small. Use N to augment the capacity.");
                assert(false);
            }
            _timestamps.resize(_timestamps.size() == 0? _timestamps.capacity():_timestamps.size() * 2, Timestamp::uninitialized_t());
        }
        _timestamps.unchecked_at(i) = TimestampT::now_steady();
    } else {
        _timestamps.push_back(TimestampT::now_steady());
    }
}

void RecordTimestamp::push(int, Packet *p) {
    rmaction(p);
    output(0).push(p);
}

#if HAVE_BATCH
void RecordTimestamp::push_batch(int, PacketBatch *batch) {
    FOR_EACH_PACKET(batch, p) {
        rmaction(p);
    }
    output(0).push_batch(batch);
}
#endif


CLICK_ENDDECLS

ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(RecordTimestamp)
