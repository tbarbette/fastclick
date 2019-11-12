/*
 * iprewriter.{cc,hh} -- rewrites packet source and destination
 * Max Poletto, Eddie Kohler
 *
 * Per-core, thread safe data structures and computational batching
 * by Georgios Katsikas and Tom Barbette
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2008-2010 Meraki, Inc.
 * Copyright (c) 2016 KTH Royal Institute of Technology
 * Copyright (c) 2017 University of Liege
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
#include "iprewriter.hh"
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
#include <click/args.hh>
#include <click/straccum.hh>
#include <click/error.hh>
#include <click/timer.hh>
#include <click/router.hh>
CLICK_DECLS

IPRewriter::IPRewriter() : _ipstate()
{
}

IPRewriter::~IPRewriter()
{
}

void *
IPRewriter::cast(const char *n)
{
    if (strcmp(n, "IPRewriterBase") == 0)
	return (IPRewriterBase *)this;
    else if (strcmp(n, "TCPRewriter") == 0)
	return (TCPRewriter *)this;
    else if (strcmp(n, "IPRewriter") == 0)
	return this;
    else
	return 0;
}

int
IPRewriter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    bool has_udp_streaming_timeout = false;
    uint32_t udp_timeouts[2];
    uint32_t udp_streaming_timeout;
    udp_timeouts[0] = 60 * 5;	// 5 minutes
    udp_timeouts[1] = 5;	// 5 seconds

    if (Args(this, errh).bind(conf)
	.read("UDP_TIMEOUT", SecondsArg(), udp_timeouts[0])
	.read("UDP_STREAMING_TIMEOUT", SecondsArg(), udp_streaming_timeout).read_status(has_udp_streaming_timeout)
	.read("UDP_GUARANTEE", SecondsArg(), udp_timeouts[1])
	.consume() < 0)
	return -1;

    if (!has_udp_streaming_timeout)
	udp_streaming_timeout = udp_timeouts[0];
    udp_timeouts[0] *= CLICK_HZ; // change timeouts to jiffies
    udp_timeouts[1] *= CLICK_HZ;
    udp_streaming_timeout *= CLICK_HZ; // IPRewriterBase handles the others

    for (unsigned i=0; i<_ipstate.weight(); i++) {
        IPState& state = _ipstate.get_value(i);
        state._udp_timeouts[0] = udp_timeouts[0];
        state._udp_timeouts[1] = udp_timeouts[1];
        state._udp_streaming_timeout = udp_streaming_timeout;
    }

    return TCPRewriter::configure(conf, errh);
}


int IPRewriter::thread_configure(ThreadReconfigurationStage stage, ErrorHandler* errh, Bitvector threads) {
	click_jiffies_t jiffies = click_jiffies();
	if (_handle_migration) {
        if (stage == THREAD_RECONFIGURE_UP_PRE) {
            set_migration(true, threads, _state);
            set_migration<IPState>(true, threads, _ipstate);
        } else if (stage == THREAD_RECONFIGURE_DOWN_PRE){
            set_migration(false, threads, _state);
            set_migration<IPState>(false, threads, _ipstate);
        }
	}
	return 0;
}



IPRewriterEntry *
IPRewriter::get_entry(int ip_p, const IPFlowID &flowid, int input)
{
    if (ip_p == IP_PROTO_TCP)
	return TCPRewriter::get_entry(ip_p, flowid, input); //Migration handled upstream
    if (ip_p != IP_PROTO_UDP)
	return 0;

    IPRewriterEntry *m = _ipstate->map.get(flowid);
    if (!m && (unsigned) input < (unsigned) _input_specs.size()) {
        IPFlowID rewritten_flowid;
        if (_handle_migration && !precopy)
            m = search_migrate_entry<IPState>(flowid, _ipstate);

        if (m) {
            rewritten_flowid = m->rewritten_flowid();
        } else {
	        IPRewriterInput &is = _input_specs[input];
            rewritten_flowid = IPFlowID::uninitialized_t();
	        if (is.rewrite_flowid(flowid, rewritten_flowid, 0) != rw_addmap) {
                return 0;
            }
        }
	    m = add_flow(0, flowid, rewritten_flowid, input);
    }
    return m;
}

IPRewriterEntry *
IPRewriter::add_flow(int ip_p, const IPFlowID &flowid,
		     const IPFlowID &rewritten_flowid, int input)
{
    if (ip_p == IP_PROTO_TCP)
	return TCPRewriter::add_flow(ip_p, flowid, rewritten_flowid, input);

    void *data = _ipstate->_udp_allocator.allocate();
    if (!data) {
        click_chatter("[%s] [Core %d]: UDP Allocator failed", class_name(), click_current_cpu_id());
	return 0;
    }

    IPRewriterInput *rwinput = &_input_specs[input];
    IPRewriterFlow *flow = new(data) IPRewriterFlow
	(rwinput, flowid, rewritten_flowid, ip_p,
	 !!_ipstate->_udp_timeouts[1],
         click_jiffies() + relevant_timeout(_ipstate->_udp_timeouts), input);

    return store_flow(flow, input, _ipstate->map, &reply_udp_map(rwinput));
}

int
IPRewriter::process(int port, Packet *p_in)
{
    WritablePacket *p = p_in->uniqueify();
    click_ip *iph = p->ip_header();
    IPState &state = _ipstate.get();

    // handle non-first fragments
    if ((iph->ip_p != IP_PROTO_TCP && iph->ip_p != IP_PROTO_UDP)
	|| !IP_FIRSTFRAG(iph)
	|| p->transport_length() < 8) {
        const IPRewriterInput &is = _input_specs[port];
        if (is.kind == IPRewriterInput::i_nochange)
            return is.foutput;
        else
            return -1;
    }

    IPFlowID flowid(p);
    HashContainer<IPRewriterEntry> *map = (iph->ip_p == IP_PROTO_TCP ?
        &_state->map : &state.map);
    if ( !map ) {
        click_chatter("[%s] [Core %d]: UDP Map is NULL", class_name(), click_current_cpu_id());
    }
    //No lock access because we are the only writer
    IPRewriterEntry *m = map->get(flowid);

    if (!m) {			// create new mapping
	IPRewriterInput &is = _input_specs.unchecked_at(port);
	IPFlowID rewritten_flowid = IPFlowID::uninitialized_t();
	int result = is.rewrite_flowid(flowid, rewritten_flowid, p, iph->ip_p == IP_PROTO_TCP ?
             0 : IPRewriterInput::mapid_iprewriter_udp);
	if (result == rw_addmap)
	    m = IPRewriter::add_flow(iph->ip_p, flowid, rewritten_flowid, port);
	if (!m) {
	    return result;
	} else if (_annos & 2)
	    m->flow()->set_reply_anno(p->anno_u8(_annos >> 2));
    }

    click_jiffies_t now_j = click_jiffies();
    IPRewriterFlow *mf = m->flow();
    if (iph->ip_p == IP_PROTO_TCP) {
	TCPFlow *tcpmf = static_cast<TCPFlow *>(mf);
	tcpmf->apply(p, m->direction(), _annos);
	if (_timeouts[click_current_cpu_id()][1])
	    tcpmf->change_expiry(_heap[click_current_cpu_id()], true, now_j + _timeouts[click_current_cpu_id()][1]);
	else
	    tcpmf->change_expiry(_heap[click_current_cpu_id()], false, now_j + tcp_flow_timeout(tcpmf));
    } else {
	UDPFlow *udpmf = static_cast<UDPFlow *>(mf);
	udpmf->apply(p, m->direction(), _annos);
	if (_ipstate->_udp_timeouts[1])
	    udpmf->change_expiry(_heap[click_current_cpu_id()], true, now_j + state._udp_timeouts[1]);
	else
	    udpmf->change_expiry(_heap[click_current_cpu_id()], false, now_j + udp_flow_timeout(udpmf, state));
    }
    if (_set_aggregate) {
        SET_AGGREGATE_ANNO(p,mf->agg());
    }

    return m->output();
}

void
IPRewriter::push(int port, Packet *p)
{
    int output_port = process(port, p);
    if ( output_port < 0 ) {
        p->kill();
        return;
    }

    output(output_port).push(p);
}

#if HAVE_BATCH
void
IPRewriter::push_batch(int port, PacketBatch *batch)
{
    auto fnt = [this,port](Packet*p){return process(port,p);};
    CLASSIFY_EACH_PACKET(noutputs() + 1,fnt,batch,checked_output_push_batch);
}
#endif

String
IPRewriter::udp_mappings_handler(Element *e, void *)
{
    IPRewriter *rw = (IPRewriter *)e;
    click_jiffies_t now = click_jiffies();
    StringAccum sa;
    for (unsigned i = 0; i < rw->_ipstate.weight(); i++) {
        for (Map::iterator iter = rw->_ipstate.get_value(i).map.begin(); iter.live(); ++iter) {
            iter->flow()->unparse(sa, iter->direction(), now);
            sa << '\n';
        }
    }
    return sa.take_string();
}

void
IPRewriter::add_handlers()
{
    add_read_handler("tcp_table", tcp_mappings_handler);
    add_read_handler("udp_table", udp_mappings_handler);
    add_read_handler("tcp_mappings", tcp_mappings_handler, 0, Handler::h_deprecated);
    add_read_handler("udp_mappings", udp_mappings_handler, 0, Handler::h_deprecated);
    set_handler("tcp_lookup", Handler::OP_READ | Handler::READ_PARAM, tcp_lookup_handler, 0);
    add_rewriter_handlers(true);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(TCPRewriter UDPRewriter)
EXPORT_ELEMENT(IPRewriter)
