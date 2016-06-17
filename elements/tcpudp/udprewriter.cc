/*
 * udprewriter.{cc,hh} -- rewrites packet source and destination
 * Max Poletto, Eddie Kohler
 *
 * Per-core, thread safe data structures and batching by Georgios Katsikas
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
#include "udprewriter.hh"
#include <click/args.hh>
#include <click/straccum.hh>
#include <click/error.hh>
#include <click/timer.hh>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
CLICK_DECLS

void
UDPRewriter::UDPFlow::apply(WritablePacket *p, bool direction, unsigned annos, bool calc_checksum)
{
	assert(p->has_network_header());
	click_ip *iph = p->ip_header();

	// IP header
	const IPFlowID &revflow = _e[!direction].flowid();
	iph->ip_src = revflow.daddr();
	iph->ip_dst = revflow.saddr();
	if (annos & 1)
		p->set_dst_ip_anno(revflow.saddr());
	if (direction && (annos & 2))
		p->set_anno_u8(annos >> 2, _reply_anno);

	// Hyper-NF: Allow to skip IP checksum calculation
	if ( calc_checksum ) {
		update_csum(&iph->ip_sum, direction, _ip_csum_delta);
	}

	// end if not first fragment
	if (!IP_FIRSTFRAG(iph))
		return;

	// TCP/UDP header
	click_udp *udph = p->udp_header();
	udph->uh_sport = revflow.dport(); // TCP ports in the same place
	udph->uh_dport = revflow.sport();
	if (iph->ip_p == IP_PROTO_TCP) {
		if (p->transport_length() >= 18)
			update_csum(&reinterpret_cast<click_tcp *>(udph)->th_sum, direction, _udp_csum_delta);
	}
	else if (iph->ip_p == IP_PROTO_UDP) {
		if (p->transport_length() >= 8 && udph->uh_sum)
			// Hyper-NF: Allow to skip UDP checksum calculation
			if ( calc_checksum ) {
				update_csum(&udph->uh_sum, direction, _udp_csum_delta);
			}
	}

	// track connection state
	if (direction)
		_tflags |= 1;
	if (_tflags < 6)
		_tflags += 2;
}

UDPRewriter::UDPRewriter()
{
#if HAVE_USER_MULTITHREAD
	_maps_no = ( click_max_cpu_ids() == 0 )? 1 : click_max_cpu_ids();
	_allocator = new SizedHashAllocator<sizeof(UDPFlow)>[_maps_no];
	//click_chatter("[%s]: Allocated %d UDP maps", class_name(), _maps_no);
#endif
}

UDPRewriter::~UDPRewriter()
{
#if HAVE_USER_MULTITHREAD
	if ( _allocator )
		delete [] _allocator;
#endif
}

void *
UDPRewriter::cast(const char *n)
{
	if (strcmp(n, "IPRewriterBase") == 0)
		return static_cast<IPRewriterBase *> (this);
	else if (strcmp(n, "UDPRewriter") == 0)
		return static_cast<UDPRewriter *> (this);
	else
		return 0;
}

int
UDPRewriter::configure(Vector<String> &conf, ErrorHandler *errh)
{
	bool dst_anno = true, has_reply_anno = false,
	has_udp_streaming_timeout, has_streaming_timeout;
	int reply_anno;
	uint32_t timeouts[2];
	timeouts[0] = 300;		// 5 minutes

	if (Args(this, errh).bind(conf)
		.read("DST_ANNO", dst_anno)
		.read("REPLY_ANNO", AnnoArg(1), reply_anno).read_status(has_reply_anno)
		.read("UDP_TIMEOUT", SecondsArg(), timeouts[0])
		.read("TIMEOUT", SecondsArg(), timeouts[0])
		.read("UDP_STREAMING_TIMEOUT", SecondsArg(), _udp_streaming_timeout).read_status(has_udp_streaming_timeout)
		.read("STREAMING_TIMEOUT", SecondsArg(), _udp_streaming_timeout).read_status(has_streaming_timeout)
		.read("UDP_GUARANTEE", SecondsArg(), timeouts[1])
		.consume() < 0)
		return -1;

	for (unsigned i=0; i<_mem_units_no; i++) {
		_timeouts[i] = timeouts;
	}

	_annos = (dst_anno ? 1 : 0) + (has_reply_anno ? 2 + (reply_anno << 2) : 0);
	if (!has_udp_streaming_timeout && !has_streaming_timeout)
		_udp_streaming_timeout = _timeouts[click_current_cpu_id()][0];
	_udp_streaming_timeout *= CLICK_HZ; // IPRewriterBase handles the others

	return IPRewriterBase::configure(conf, errh);
}

IPRewriterEntry *
UDPRewriter::add_flow(int ip_p, const IPFlowID &flowid,
			const IPFlowID &rewritten_flowid, int input)
{
	void *data = _allocator[click_current_cpu_id()].allocate();
	if ( !data )
		return 0;

	UDPFlow *flow = new(data) UDPFlow
		(&_input_specs[input], flowid, rewritten_flowid, ip_p,
		 !!_timeouts[click_current_cpu_id()][1], click_jiffies() + 
		 relevant_timeout(_timeouts[click_current_cpu_id()]));

	return store_flow(flow, input, _map[click_current_cpu_id()]);
}

int
UDPRewriter::process(int port, Packet *p_in) {
	WritablePacket *p = p_in->uniqueify();
	if (!p) {
		return -1;
	}
	click_ip *iph = p->ip_header();

	// Handle non-TCP and non-first fragments
	int ip_p = iph->ip_p;
	if ((ip_p != IP_PROTO_TCP && ip_p != IP_PROTO_UDP && ip_p != IP_PROTO_DCCP)
		|| !IP_FIRSTFRAG(iph)
		|| p->transport_length() < 8) {
		const IPRewriterInput &is = _input_specs[port];
		if (is.kind == IPRewriterInput::i_nochange)
			return is.foutput;
		return -1;
	}

	IPFlowID flowid(p);
	IPRewriterEntry *m = _map[click_current_cpu_id()].get(flowid);

	if (!m) {                   // create new mapping
		IPRewriterInput &is = _input_specs.unchecked_at(port);
		IPFlowID rewritten_flowid = IPFlowID::uninitialized_t();
		int result = is.rewrite_flowid(flowid, rewritten_flowid, p);
		if (result == rw_addmap)
			m = UDPRewriter::add_flow(ip_p, flowid, rewritten_flowid, port);
		if (!m)
			return result;
		else if (_annos & 2)
			m->flow()->set_reply_anno(p->anno_u8(_annos >> 2));
	}

	UDPFlow *mf = static_cast<UDPFlow *>(m->flow());
	mf->apply(p, m->direction(), _annos, _calc_checksum);

	click_jiffies_t now_j = click_jiffies();
	if (_timeouts[click_current_cpu_id()][1])
		mf->change_expiry(_heap[click_current_cpu_id()], true, now_j + _timeouts[click_current_cpu_id()][1]);
	else
		mf->change_expiry(_heap[click_current_cpu_id()], false, now_j + udp_flow_timeout(mf));

	return m->output();
}

void
UDPRewriter::push_packet(int port, Packet *p)
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
UDPRewriter::push_batch(int port, PacketBatch *batch)
{
	unsigned short outports = noutputs();
	PacketBatch *out[outports];
	bzero(out, sizeof(PacketBatch*) *outports);
	PacketBatch *next = ((batch != NULL)? static_cast<PacketBatch*>(batch->next()) : NULL );
	PacketBatch *p = batch;
	PacketBatch *last = NULL;
	int last_o = -1;
	int passed = 0;
	int count  = 0;
	for ( ; p != NULL;p=next,next=(p==0?0:static_cast<PacketBatch*>(p->next())) ) {
		// The actual job of this element
		int o = process(port, p);

		if (o < 0 || o>=(outports)) o = (outports - 1);

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
UDPRewriter::dump_mappings_handler(Element *e, void *)
{
	UDPRewriter *rw = static_cast<UDPRewriter *> (e);
	click_jiffies_t now = click_jiffies();
	StringAccum sa;
	for (Map::iterator iter = rw->_map[click_current_cpu_id()].begin(); iter.live(); ++iter) {
		iter->flow()->unparse(sa, iter->direction(), now);
		sa << '\n';
	}
	return sa.take_string();
}

void
UDPRewriter::add_handlers()
{
	add_read_handler("table", dump_mappings_handler);
	add_read_handler("mappings", dump_mappings_handler, 0, Handler::h_deprecated);
	add_rewriter_handlers(true);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(IPRewriterBase)
EXPORT_ELEMENT(UDPRewriter)
