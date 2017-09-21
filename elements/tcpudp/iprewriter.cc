/*
 * iprewriter.{cc,hh} -- rewrites packet source and destination
 * Max Poletto, Eddie Kohler
 *
 * Per-core, thread safe data structures and computational batching
 * by Georgios Katsikas
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2008-2010 Meraki, Inc.
 * Copyright (c) 2016 KTH Royal Institute of Technology
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

IPRewriter::IPRewriter()
{
    _mem_units_no = ( click_max_cpu_ids() == 0 )? 1 : click_max_cpu_ids();

    _udp_map       = new Map[_mem_units_no];
    _udp_allocator = new SizedHashAllocator<sizeof(UDPFlow)>[_mem_units_no];
    _udp_timeouts  = new uint32_t*[_mem_units_no];
    _udp_streaming_timeout = new uint32_t[_mem_units_no];
    for (unsigned i=0; i<_mem_units_no; i++) {
        _udp_timeouts[i] = new uint32_t[2];
    }
    //click_chatter("[%s]: Allocated %d memory units", class_name(), _mem_units_no);
}

IPRewriter::~IPRewriter()
{
    delete [] _udp_map;
    if ( _udp_allocator )
        delete [] _udp_allocator;
    for (unsigned i=0; i<_mem_units_no; i++) {
        if ( _udp_timeouts[i] ) {
            //delete [] _udp_timeouts[i];
        }
    }
    delete [] _udp_timeouts;
    delete [] _udp_streaming_timeout;
    //click_chatter("[%s]: Released %d memory units", class_name(), _mem_units_no);
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

    for (unsigned i=0; i<_mem_units_no; i++) {
        _udp_timeouts[i] = udp_timeouts;
        _udp_streaming_timeout[i] = udp_streaming_timeout;
    }

    return TCPRewriter::configure(conf, errh);
}

inline IPRewriterEntry *
IPRewriter::get_entry(int ip_p, const IPFlowID &flowid, int input)
{
    if (ip_p == IP_PROTO_TCP)
	return TCPRewriter::get_entry(ip_p, flowid, input);
    if (ip_p != IP_PROTO_UDP)
	return 0;
    IPRewriterEntry *m = _udp_map[click_current_cpu_id()].get(flowid);
    if (!m && (unsigned) input < (unsigned) _input_specs.size()) {
	IPRewriterInput &is = _input_specs[input];
	IPFlowID rewritten_flowid = IPFlowID::uninitialized_t();
	if (is.rewrite_flowid(flowid, rewritten_flowid, 0, IPRewriterInput::mapid_iprewriter_udp) == rw_addmap)
	    m = IPRewriter::add_flow(0, flowid, rewritten_flowid, input);
    }
    return m;
}

IPRewriterEntry *
IPRewriter::add_flow(int ip_p, const IPFlowID &flowid,
		     const IPFlowID &rewritten_flowid, int input)
{
    if (ip_p == IP_PROTO_TCP)
	return TCPRewriter::add_flow(ip_p, flowid, rewritten_flowid, input);

    void *data = _udp_allocator[click_current_cpu_id()].allocate();
    if (!data) {
        click_chatter("[%s] [Core %d]: UDP Allocator failed", class_name(), click_current_cpu_id());
	return 0;
    }

    IPRewriterInput *rwinput = &_input_specs[input];
    IPRewriterFlow *flow = new(data) IPRewriterFlow
	(rwinput, flowid, rewritten_flowid, ip_p,
	 !!_udp_timeouts[click_current_cpu_id()][1],
         click_jiffies() + relevant_timeout(_udp_timeouts[click_current_cpu_id()]));

    return store_flow(flow, input, _udp_map[click_current_cpu_id()], &reply_udp_map(rwinput));
}

int
IPRewriter::process(int port, Packet *p_in)
{
    WritablePacket *p = p_in->uniqueify();
    click_ip *iph = p->ip_header();

    // handle non-first fragments
    if ((iph->ip_p != IP_PROTO_TCP && iph->ip_p != IP_PROTO_UDP)
	|| !IP_FIRSTFRAG(iph)
	|| p->transport_length() < 8) {
	const IPRewriterInput &is = _input_specs[port];
	if (is.kind == IPRewriterInput::i_nochange)
	    output(is.foutput).push(p);
	else
	    p->kill();
	return -1;
    }

    IPFlowID flowid(p);
    HashContainer<IPRewriterEntry> *map = (iph->ip_p == IP_PROTO_TCP ?
        &_map[click_current_cpu_id()] : &_udp_map[click_current_cpu_id()]);
    if ( !map ) {
        click_chatter("[%s] [Core %d]: UDP Map is NULL", class_name(), click_current_cpu_id());
    }
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
	if (_udp_timeouts[click_current_cpu_id()][1])
	    udpmf->change_expiry(_heap[click_current_cpu_id()], true, now_j + _udp_timeouts[click_current_cpu_id()][1]);
	else
	    udpmf->change_expiry(_heap[click_current_cpu_id()], false, now_j + udp_flow_timeout(udpmf));
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
    unsigned short outports = noutputs();
    PacketBatch* out[outports];
    bzero(out,sizeof(PacketBatch*)*outports);
    PacketBatch *next = ((batch != NULL)? static_cast<PacketBatch*>(batch->next()) : NULL );
    PacketBatch *p = batch;
    PacketBatch *last = NULL;
    int last_o = -1;
    int passed = 0;
    int count  = 0;
    for ( ; p != NULL;p=next,next=(p==0?0:static_cast<PacketBatch*>(p->next())) ) {
        // The actual job of this element
        int o = process(port, p);

        if (o < 0 || o>=(outports))
            o = (outports - 1);

        if (o == last_o) {
            passed ++;
        }
        else {
            if ( !last ) {
                out[o] = p;
                p->set_count(1);
                p->set_tail(p);
            }
            else {
                out[last_o]->set_tail(last);
                out[last_o]->set_count(out[last_o]->count() + passed);
                if (!out[o]) {
                    out[o] = p;
                    out[o]->set_count(1);
                    out[o]->set_tail(p);
                }
                else {
                    out[o]->append_packet(p);
                }
                passed = 0;
            }
        }
        last = p;
        last_o = o;
        count++;
    }

    if (passed) {
        out[last_o]->set_tail(last);
        out[last_o]->set_count(out[last_o]->count() + passed);
    }

    int i = 0;
    for (; i < outports; i++) {
        if (out[i]) {
            out[i]->tail()->set_next(NULL);
            checked_output_push_batch(i, out[i]);
        }
    }
}
#endif

String
IPRewriter::udp_mappings_handler(Element *e, void *)
{
    IPRewriter *rw = (IPRewriter *)e;
    click_jiffies_t now = click_jiffies();
    StringAccum sa;
    for (Map::iterator iter = rw->_udp_map[click_current_cpu_id()].begin(); iter.live(); ++iter) {
	iter->flow()->unparse(sa, iter->direction(), now);
	sa << '\n';
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
