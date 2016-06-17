/*
 * ipsynthesizer.{cc,hh} -- rewrites TCP/UDP packet source and destination
 * while (optionally) applying post-routing operations
 * Extends IPRewriter written by Max Poletto, Eddie Kohler
 * Per-core, thread safe data structures, batching and post-routing operations
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
#include "ipsynthesizer.hh"
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
#include <click/args.hh>
#include <click/straccum.hh>
#include <click/error.hh>
#include <click/timer.hh>
#include <click/router.hh>
#include <click/packet_anno.hh>
CLICK_DECLS

IPSynthesizer::IPSynthesizer()
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

IPSynthesizer::~IPSynthesizer()
{
	if ( _udp_map )
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
IPSynthesizer::cast(const char *n)
{
	if      (strcmp(n, "IPRewriterBase") == 0)
		return static_cast<IPRewriterBase *> (this);
	else if (strcmp(n, "TCPRewriter") == 0)
		return static_cast<TCPRewriter *> (this);
	else if (strcmp(n, "IPSynthesizer") == 0)
		return this;
	else
		return 0;
}

int
IPSynthesizer::configure(Vector<String> &conf, ErrorHandler *errh)
{
	bool has_udp_streaming_timeout = false;
	uint32_t udp_timeouts[2];
	uint32_t udp_streaming_timeout;
	udp_timeouts[0] = 60 * 5;	// 5 minutes
	udp_timeouts[1] = 5;	    // 5 seconds

	if (Args(this, errh).bind(conf)
		.read("UDP_TIMEOUT", SecondsArg(), udp_timeouts[0])
		.read("UDP_STREAMING_TIMEOUT", SecondsArg(), udp_streaming_timeout).read_status(has_udp_streaming_timeout)
		.read("UDP_GUARANTEE", SecondsArg(), udp_timeouts[1])
		.consume() < 0)
		return -1;

	if (!has_udp_streaming_timeout)
		udp_streaming_timeout = udp_timeouts[0];
	udp_timeouts[0] *= CLICK_HZ;       // Change timeouts to jiffies
	udp_timeouts[1] *= CLICK_HZ;
	udp_streaming_timeout *= CLICK_HZ; // IPRewriterBase handles the others

	for (unsigned i=0; i<_mem_units_no; i++) {
		_udp_timeouts[i] = udp_timeouts;
		_udp_streaming_timeout[i] = udp_streaming_timeout;
	}

	return TCPRewriter::configure(conf, errh);
}

inline IPRewriterEntry *
IPSynthesizer::get_entry(int ip_p, const IPFlowID &flowid, int input)
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
			m = IPSynthesizer::add_flow(0, flowid, rewritten_flowid, input);
	}

	return m;
}

IPRewriterEntry *
IPSynthesizer::add_flow(int ip_p, const IPFlowID &flowid,
						 const IPFlowID &rewritten_flowid, int input)
{
	if (ip_p == IP_PROTO_TCP)
		return TCPRewriter::add_flow(ip_p, flowid, rewritten_flowid, input);

	void *data = _udp_allocator[click_current_cpu_id()].allocate();
	if ( !data ) {
		click_chatter("[%s] [Core %d]: UDP Allocator failed", class_name(), click_current_cpu_id());
		return 0;
	}

	IPRewriterInput *rwinput = &_input_specs[input];
	IPRewriterFlow  *flow    = new(data) IPRewriterFlow
		(rwinput, flowid, rewritten_flowid, ip_p,
		 !!_udp_timeouts[click_current_cpu_id()][1], click_jiffies() + relevant_timeout(_udp_timeouts[click_current_cpu_id()]));

	return store_flow(flow, input, _udp_map[click_current_cpu_id()], &reply_udp_map(rwinput));
}

int
IPSynthesizer::process(int port, Packet *p_in)
{
	WritablePacket *p = p_in->uniqueify();
	if (!p) {
		return -1;
	}

	click_ip *iph = p->ip_header();

	// handle non-first fragments
	if ((iph->ip_p != IP_PROTO_TCP && iph->ip_p != IP_PROTO_UDP)
		|| !IP_FIRSTFRAG(iph)
		|| p->transport_length() < 8) {
		const IPRewriterInput &is = _input_specs[port];
		if (is.kind == IPRewriterInput::i_nochange) {
			return is.foutput;
		}
		return -1;
	}

	// Hyper-NF: Apply post-routing elements
	// such as DropBroadcasts, FixIPSrc, DecIPTTL, ect.
	if ( !combine_ip_elements(p_in) ) {
		return -1;
	}

	IPFlowID flowid(p);
	HashContainer<IPRewriterEntry> *map = (iph->ip_p == IP_PROTO_TCP ?
			&_map[click_current_cpu_id()] : &_udp_map[click_current_cpu_id()]);
	if ( !map ) {
		click_chatter("[%s] [Core %d]: UDP Map is NULL", class_name(), click_current_cpu_id());
	}
	IPRewriterEntry *m = map->get(flowid);

	if (!m) {                   // create new mapping
		IPRewriterInput &is = _input_specs.unchecked_at(port);
		IPFlowID rewritten_flowid = IPFlowID::uninitialized_t();
		int result = is.rewrite_flowid(flowid, rewritten_flowid, p, iph->ip_p == IP_PROTO_TCP ?
					0 : IPRewriterInput::mapid_iprewriter_udp);
		if (result == rw_addmap) {
			m = IPSynthesizer::add_flow(iph->ip_p, flowid, rewritten_flowid, port);
			//click_chatter("[%s] [Core %d]: Flow added", class_name(), click_current_cpu_id());
			//print_flow_info(m, iph);
		}
		if (!m)
			return result;
		else if (_annos & 2)
			m->flow()->set_reply_anno(p->anno_u8(_annos >> 2));
	}

	click_jiffies_t now_j = click_jiffies();
	IPRewriterFlow *mf = m->flow();
	if (iph->ip_p == IP_PROTO_TCP) {
		TCPFlow *tcpmf = static_cast<TCPFlow *>(mf);
		tcpmf->apply(p, m->direction(), _annos, _calc_checksum);
		if (_timeouts[click_current_cpu_id()][1])
			tcpmf->change_expiry(_heap[click_current_cpu_id()], true, now_j + _timeouts[click_current_cpu_id()][1]);
		else
			tcpmf->change_expiry(_heap[click_current_cpu_id()], false, now_j + tcp_flow_timeout(tcpmf));
	}
	else {
		UDPFlow *udpmf = static_cast<UDPFlow *>(mf);
		udpmf->apply(p, m->direction(), _annos, _calc_checksum);
		if (_udp_timeouts[click_current_cpu_id()][1])
			udpmf->change_expiry(_heap[click_current_cpu_id()], true, now_j + _udp_timeouts[click_current_cpu_id()][1]);
		else
			udpmf->change_expiry(_heap[click_current_cpu_id()], false, now_j + udp_flow_timeout(udpmf));
	}

	// Fragment IP packets if requested to do so.
	// If fragmentation occurs, batching is disabled
	// since the fragment function is calling the normal
	// push method to send all the parts of the packet out.
	if ( (p->network_length() > (int) this->_mtu) && (_ip_fragment) ) {
		fragment(p, m->output());
	}
	return m->output();
}

bool
IPSynthesizer::combine_ip_elements(Packet *p_in)
{
	int do_cksum = 0;
	int problem_offset = -1;

	// DropBroadcasts
	if (  (this->_drop_bcast) &&
		(p_in->packet_type_anno() == Packet::BROADCAST || p_in->packet_type_anno() == Packet::MULTICAST) ) {
		return false;
	}

	// IPGWOptions
	WritablePacket *p = p_in->uniqueify();
	assert(p->has_network_header());
	click_ip *ip = p->ip_header();
	unsigned hlen = (ip->ip_hl << 2);

	if ( (this->_ipgw_opt) && (hlen > sizeof(click_ip)) ) {
		uint8_t *woa = p->network_header();
		int hlen = p->network_header_length();

		int oi;
		for (oi = sizeof(click_ip); oi < hlen; ) {
			// handle one-byte options
			unsigned type = woa[oi];
			if (type == IPOPT_NOP) {
				oi++;
				continue;
			} else if (type == IPOPT_EOL)
				/* end of option list */
				break;

			// otherwise, get option length
			int xlen = woa[oi + 1];
			if (xlen < 2 || oi + xlen > hlen) {
				// bad length
				problem_offset = oi + 1; // to point at length
				goto ipgw_send_error;
			} else if (type != IPOPT_RR && type != IPOPT_TS) {
				// not for us to process
				oi += xlen;
				continue;
			}

			// Have a writable packet already
			if(type == IPOPT_RR){
				/*
				 * Record Route.
				 * Apparently the pointer (oa[oi+2]) is 1-origin.
				 */
				int p = woa[oi+2] - 1;
				if (p >= 3 && p + 4 <= xlen) {
					memcpy(woa + oi + p, &_my_ip, 4);
					woa[oi+2] += 4;
					do_cksum = 1;
				} else if (p != xlen) {
					problem_offset = oi + 2;
					goto ipgw_send_error;
				}
			}
			else if(type == IPOPT_TS){
				/*
				 * Timestamp Option.
				 * We can't do a good job with the pre-specified mode (flg=3),
				 * since we don't know all our i/f addresses.
				 */
				int p = woa[oi+2] - 1;
				int oflw = woa[oi+3] >> 4;
				int flg = woa[oi+3] & 0xf;
				bool overflowed = 0;

				Timestamp now = Timestamp::now();
				int ms = htonl((now.sec() % 86400)*1000 + now.msec());

				if(p < 4){
					problem_offset = oi + 2;
					goto ipgw_send_error;
				}
				else if(flg == 0){
					/* 32-bit timestamps only */
					if(p+4 <= xlen){
						memcpy(woa + oi + p, &ms, 4);
						woa[oi+2] += 4;
						do_cksum = 1;
					}
					else
						overflowed = 1;
				}
				else if(flg == 1){
					/* IP address followed by timestamp */
					if(p+8 <= xlen){
						memcpy(woa + oi + p, &_my_ip, 4);
						memcpy(woa + oi + p + 4, &ms, 4);
						woa[oi+2] += 8;
						do_cksum = 1;
					}
					else
						overflowed = 1;
				}
				else if (flg == 3 && p + 8 <= xlen) {
					/* only if it's my address */
					if (memcmp(woa + oi + p, &_my_ip, 4) == 0) {
						memcpy(woa + oi + p + 4, &ms, 4);
						woa[oi+2] += 8;
						do_cksum = 1;
					}
				}
				else {
					problem_offset = oi + 3;
					goto ipgw_send_error;
				}
				if (overflowed) {
					if (oflw < 15) {
						woa[oi+3] = ((oflw + 1) << 4) | flg;
						do_cksum = 1;
					}
					else {
						problem_offset = oi + 3;
						goto ipgw_send_error;
					}
				}
			}
			oi += xlen;
		}
	}

	// FixIPSrc
	if ( (this->_fix_ip_src) && FIX_IP_SRC_ANNO(p) )  {
		SET_FIX_IP_SRC_ANNO(p, 0);
		ip->ip_src = _my_ip;
		do_cksum = 1;
	}

	// For IPGWOptions / FixIPSrc
	if (do_cksum) {
		ip->ip_sum = 0;
		ip->ip_sum = click_in_cksum(p->data(), hlen);
	}

	// DecIPTTL
	if ( this->_dec_ip_ttl ) {
		// Safe decrement
		if ( ip->ip_ttl > 1 ) {
			ip->ip_ttl --;
			// Checksum is not mandatory anymore
			if ( _calc_checksum ) {
				unsigned long sum = (~ntohs(ip->ip_sum) & 0xFFFF) + 0xFEFF;
				ip->ip_sum = ~htons(sum + (sum >> 16));
			}
		}
		// End flow if TTL is 0
		else {
			return false;
		}
	}

	// Everything OK!
	return true;

	// Problem from IPGWOptions
	ipgw_send_error:
		return false;
}

void
IPSynthesizer::fragment(Packet *p_in, const int &output_port)
{
	const click_ip *ip_in = p_in->ip_header();
	int hlen = ip_in->ip_hl << 2;
	int first_dlen = (_mtu - hlen) & ~7;
	int in_dlen = ntohs(ip_in->ip_len) - hlen;

	if (((ip_in->ip_off & htons(IP_DF)) && _honor_df) || first_dlen < 8) {
		if (_verbose || _drops < 5)
			click_chatter("[%s] [Core %d]: IPFragmenter(%d) DF %p{ip_ptr} %p{ip_ptr} len=%d",
							class_name(), click_current_cpu_id(), _mtu, &ip_in->ip_src,
							&ip_in->ip_dst, p_in->length());
		_drops++;
		p_in->kill();
		//checked_output_push(1, p_in);
		return;
	}

	if (_verbose)
		click_chatter(	"[%s] [Core %d]: IP fragmentation unbatches the packets to produce smaller ones."
						"Performance will be reduced", class_name(), click_current_cpu_id());

	// Make sure we can modify the packet
	WritablePacket *p = p_in->uniqueify();
	if (!p)
		return;
	click_ip *ip = p->ip_header();

	// Output the first fragment
	// If we're cheating the DF bit, we can't trust the ip_id; set to random.
	if (ip->ip_off & htons(IP_DF)) {
		ip->ip_id = click_random();
		ip->ip_off &= ~htons(IP_DF);
	}

	bool had_mf = (ip->ip_off & htons(IP_MF)) != 0;
	ip->ip_len = htons(hlen + first_dlen);
	ip->ip_off |= htons(IP_MF);
	ip->ip_sum = 0;
	ip->ip_sum = click_in_cksum((const unsigned char *)ip, hlen);
	Packet *first_fragment = p->clone();
	first_fragment->take(p->length() - p->network_header_offset() - hlen - first_dlen);
	output(output_port).push(first_fragment);
	_fragments++;

	// Output the remaining fragments
	int out_hlen = sizeof(click_ip) + optcopy(ip, 0);

	for (int off = first_dlen; off < in_dlen; ) {
		// Prepare packet
		int out_dlen = (_mtu - out_hlen) & ~7;
		if (out_dlen + off > in_dlen)
			out_dlen = in_dlen - off;

		WritablePacket *q = Packet::make(_headroom, 0, out_hlen + out_dlen, 0);
		if (q) {
			q->set_network_header(q->data(), out_hlen);
			click_ip *qip = q->ip_header();

			memcpy(qip, ip, sizeof(click_ip));
			optcopy(ip, qip);
			memcpy(q->transport_header(), p->transport_header() + off, out_dlen);

			qip->ip_hl = out_hlen >> 2;
			qip->ip_off = htons(ntohs(ip->ip_off) + (off >> 3));
			if (out_dlen + off >= in_dlen && !had_mf)
				qip->ip_off &= ~htons(IP_MF);
			qip->ip_len = htons(out_hlen + out_dlen);
			qip->ip_sum = 0;
			qip->ip_sum = click_in_cksum((const unsigned char *)qip, out_hlen);

			q->copy_annotations(p);

			output(output_port).push(q);
			_fragments++;
		}
		off += out_dlen;
	}

	p->kill();
}

int
IPSynthesizer::optcopy(const click_ip *ip1, click_ip *ip2)
{
	const uint8_t* oin = (const uint8_t*) (ip1 + 1);
	const uint8_t* oin_end = oin + (ip1->ip_hl << 2) - sizeof(click_ip);
	uint8_t *oout = (uint8_t *) (ip2 + 1);
	int outpos = 0;

	while (oin < oin_end)
		if (*oin == IPOPT_NOP)  // don't copy NOP
			++oin;
		else if (*oin == IPOPT_EOL
				|| oin + 1 == oin_end
				|| oin[1] < 2
				|| oin + oin[1] > oin_end)
			break;
		else {
			if (*oin & 0x80) {	// copy the option
				if (ip2)
					memcpy(oout + outpos, oin, oin[1]);
				outpos += oin[1];
			}
			oin += oin[1];
		}

	for (; (outpos & 3) != 0; outpos++)
		if (ip2)
			oout[outpos] = IPOPT_EOL;

	return outpos;
}

void
IPSynthesizer::push_packet(int port, Packet *p)
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
IPSynthesizer::push_batch(int port, PacketBatch *batch)
{
	unsigned short outports = noutputs();
	PacketBatch* out[outports];
	bzero(out,sizeof(PacketBatch*)*outports);
	PacketBatch* next = ((batch != NULL)? static_cast<PacketBatch*>(batch->next()) : NULL );
	PacketBatch* p = batch;
	PacketBatch* last = NULL;
	int last_o = -1;
	int passed = 0;
	int count  = 0;

	for ( ;p != NULL; p=next,next=(p==0?0:static_cast<PacketBatch*>(p->next())) ) {
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
IPSynthesizer::udp_mappings_handler(Element *e, void *)
{
	IPSynthesizer *rw = static_cast<IPSynthesizer *> (e);
	click_jiffies_t now = click_jiffies();
	StringAccum sa;

	for (Map::iterator iter = rw->_udp_map[click_current_cpu_id()].begin(); iter.live(); ++iter) {
		iter->flow()->unparse(sa, iter->direction(), now);
		sa << '\n';
	}
	return sa.take_string();
}

void
IPSynthesizer::add_handlers()
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
EXPORT_ELEMENT(IPSynthesizer)
