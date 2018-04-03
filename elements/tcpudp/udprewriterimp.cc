/*
 * udprewriter.{cc,hh} -- rewrites packet source and destination
 * Max Poletto, Eddie Kohler
 *
 * Per-core, thread safe data structures and batching by Georgios Katsikas and Tom Barbette
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
#include "udprewriterimp.hh"
#include <click/args.hh>
#include <click/straccum.hh>
#include <click/error.hh>
#include <click/timer.hh>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
CLICK_DECLS

UDPRewriterIMP::UDPRewriterIMP() : _allocator()
{
}

UDPRewriterIMP::~UDPRewriterIMP()
{
}

void *
UDPRewriterIMP::cast(const char *n)
{
    if (strcmp(n, "IPRewriterBase") == 0)
	return (IPRewriterBase *)this;
    else if (strcmp(n, "UDPRewriterIMP") == 0)
	return (UDPRewriterIMP *)this;
    else
	return 0;
}

int
UDPRewriterIMP::configure(Vector<String> &conf, ErrorHandler *errh)
{
    bool dst_anno = true, has_reply_anno = false,
	has_udp_streaming_timeout, has_streaming_timeout;
    int reply_anno;
    uint32_t utimeouts[2];
    utimeouts[0] = 300;		// 5 minutes
    utimeouts[1] = default_guarantee;

    if (Args(this, errh).bind(conf)
	.read("DST_ANNO", dst_anno)
	.read("REPLY_ANNO", AnnoArg(1), reply_anno).read_status(has_reply_anno)
	.read("UDP_TIMEOUT", SecondsArg(), utimeouts[0])
	.read("TIMEOUT", SecondsArg(), utimeouts[0])
	.read("UDP_STREAMING_TIMEOUT", SecondsArg(), _udp_streaming_timeout).read_status(has_udp_streaming_timeout)
	.read("STREAMING_TIMEOUT", SecondsArg(), _udp_streaming_timeout).read_status(has_streaming_timeout)
	.read("UDP_GUARANTEE", SecondsArg(), utimeouts[1])
	.consume() < 0)
	return -1;

    initialize_timeout(0, utimeouts[0]);
    initialize_timeout(1, utimeouts[1]);

    _annos = (dst_anno ? 1 : 0) + (has_reply_anno ? 2 + (reply_anno << 2) : 0);
    if (!has_udp_streaming_timeout && !has_streaming_timeout) {
    	_udp_streaming_timeout = utimeouts[0];
    }
    _udp_streaming_timeout *= CLICK_HZ; // IPRewriterBase handles the others

    return IPRewriterBaseIMP::configure(conf, errh);
}

IPRewriterEntry *
UDPRewriterIMP::add_flow(int ip_p, const IPFlowID &flowid,
		      const IPFlowID &rewritten_flowid, int input)
{
    void *data = _allocator->allocate();
    if (!data)
        return 0;

    UDPFlow *flow = new(data) UDPFlow
	((IPRewriterInput*)&input_specs(input), flowid, rewritten_flowid, ip_p,
	 !!timeouts()[1], click_jiffies() +
         relevant_timeout(timeouts()));

    return store_flow(flow, input, map());
}

int
UDPRewriterIMP::process(int port, Packet *p_in)
{
    WritablePacket *p = p_in->uniqueify();
    if (!p) {
        return -2;
    }

    click_ip *iph = p->ip_header();

    // handle non-TCP and non-first fragments
    int ip_p = iph->ip_p;
    if ((ip_p != IP_PROTO_TCP && ip_p != IP_PROTO_UDP && ip_p != IP_PROTO_DCCP)
	|| !IP_FIRSTFRAG(iph)
	|| p->transport_length() < 8) {
        const IPRewriterInputIMP &is = input_specs(port);
        if (is.kind == IPRewriterInput::i_nochange)
            return is.foutput;
        else
            return -1;
    }

    IPFlowID flowid(p);
    IPRewriterEntry *m = map().get(flowid);

    if (!m) {			// create new mapping
        IPRewriterInput &is = input_specs_unchecked(port);
        IPFlowID rewritten_flowid = IPFlowID::uninitialized_t();

        int result = is.rewrite_flowid(flowid, rewritten_flowid, p);
        if (result == rw_addmap) {
            m = UDPRewriterIMP::add_flow(ip_p, flowid, rewritten_flowid, port);
        }

        if (!m) {
            return result;
        } else if (_annos & 2) {
            m->flowimp()->set_reply_anno(p->anno_u8(_annos >> 2));
        }
    }

    UDPFlow *mf = static_cast<UDPFlow *>(m->flowimp());
    mf->apply(p, m->direction(), _annos);

    click_jiffies_t now_j = click_jiffies();
    if (timeouts()[1])
	mf->change_expiry(heap(), true, now_j + timeouts()[1]);
    else
	mf->change_expiry(heap(), false, now_j + udp_flow_timeout(mf));

    return m->output();
}

void
UDPRewriterIMP::push(int port, Packet *p)
{
    int output_port = process(port, p);
    if (output_port < 0) {
        if (likely(output_port) == -1)
            p->kill();
        return;
    }

    checked_output_push(output_port, p);
}

#if HAVE_BATCH
void
UDPRewriterIMP::push_batch(int port, PacketBatch *batch)
{
    auto fnt = [this,port](Packet*p){return process(port,p);};
    CLASSIFY_EACH_PACKET(noutputs() + 1,fnt,batch,checked_output_push_batch);
}
#endif

String
UDPRewriterIMP::dump_mappings_handler(Element *e, void *)
{
    UDPRewriterIMP *rw = (UDPRewriterIMP *)e;
    StringAccum sa;
    rw->dump_mappings(sa);
    return sa.take_string();
}

void
UDPRewriterIMP::add_handlers()
{
    add_read_handler("table", dump_mappings_handler);
    add_read_handler("mappings", dump_mappings_handler, 0, Handler::h_deprecated);
    add_rewriter_handlers(true);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(IPRewriterBase)
EXPORT_ELEMENT(UDPRewriterIMP)
