// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * IPRewriterBaseIMP.{cc,hh} -- rewrites packet source and destination
 * Eddie Kohler
 * original versions by Eddie Kohler and Max Poletto
 *
 * Per-core, thread safe data structures by Georgios Katsikas
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2001 International Computer Science Institute
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
#include "iprewriterbaseimp.hh"
#include "elements/ip/iprwpatterns.hh"
#include "elements/ip/iprwmapping.hh"
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
#include <click/llrpc.h>
#include <click/args.hh>
#include <click/straccum.hh>
#include <click/error.hh>
#include <click/algorithm.hh>
#include <click/heap.hh>
#include "iprwpattern.hh"

#ifdef CLICK_LINUXMODULE
#include <click/cxxprotect.h>
CLICK_CXX_PROTECT
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
# include <asm/softirq.h>
#endif
#include <net/sock.h>
CLICK_CXX_UNPROTECT
#include <click/cxxunprotect.h>
#endif

CLICK_DECLS

//
// IPMapperIMP
//

void
IPMapperIMP::notify_rewriter(IPRewriterBaseAncestor *, IPRewriterInput *, ErrorHandler *)
{
}

int
IPMapperIMP::rewrite_flowid(IPRewriterInputAncestor *, const IPFlowID &, IPFlowID &,
			 Packet *, int)
{
    return IPRewriterBaseIMP::rw_drop;
}

//
// IPRewriterBaseIMP
//

IPRewriterBaseIMP::ThreadState::ThreadState() : heap(0), timeouts(),  map(), gc_timer() {
	heap = new IPRewriterHeapIMP;
    timeouts[0] = default_timeout;
    timeouts[1] = default_guarantee;
}

IPRewriterBaseIMP::ThreadState::~ThreadState() {

}

IPRewriterBaseIMP::IPRewriterBaseIMP()
    : _state() 
{
    _gc_interval_sec = default_gc_interval;
}

IPRewriterBaseIMP::~IPRewriterBaseIMP()
{
	static_assert(sizeof(IPRewriterInputIMP) == sizeof(IPRewriterInput), "IPRewriterInputIMP must be castable to IPRewriterInput");
	for (unsigned i=0; i<_state.weight(); i++) {
		if (_state.get_value(i).heap)
			_state.get_value(i).heap->unuse();
	}
}


int
IPRewriterBaseIMP::parse_input_spec(const String &line, IPRewriterInputIMP &is,
				 int input_number, ErrorHandler *errh)
{
    PrefixErrorHandler cerrh(errh, "input spec " + String(input_number) + ": ");
    String word, rest;
    if (!cp_word(line, &word, &rest))
	return cerrh.error("empty argument");
    cp_eat_space(rest);
    is.kind = IPRewriterInputIMP::i_drop;
    is.owner_imp = this;
    is.owner_input = input_number;
    is.set_reply_element(this);

    if (word == "pass" || word == "passthrough" || word == "nochange") {
	int32_t outnum = 0;
	if (rest && !IntArg().parse(rest, outnum))
	    return cerrh.error("syntax error, expected %<nochange [OUTPUT]%>");
	else if ((unsigned) outnum >= (unsigned) noutputs())
	    return cerrh.error("output port out of range");
	is.kind = IPRewriterInputIMP::i_nochange;
	is.foutput = outnum;

    } else if (word == "keep") {
	Vector<String> words;
	cp_spacevec(rest, words);
	if (!IPRewriterPatternIMP::parse_ports(words, &is, this, &cerrh))
	    return -1;
	if ((unsigned) is.foutput >= (unsigned) noutputs()
	    || (unsigned) is.routput >= (unsigned) is.reply_element()->noutputs())
	    return cerrh.error("output port out of range");
	is.kind = IPRewriterInputIMP::i_keep;

    } else if (word == "drop" || word == "discard") {
	if (rest)
	    return cerrh.error("syntax error, expected %<%s%>", word.c_str());

    } else if (word == "pattern" || word == "xpattern") {
	if (!IPRewriterPatternIMP::parse_with_ports(rest, &is, this, &cerrh))
	    return -1;
	if ((unsigned) is.foutput >= (unsigned) noutputs()
	    || (unsigned) is.routput >= (unsigned) is.reply_element()->noutputs())
	    return cerrh.error("output port out of range");
	is.u.pattern->use();
	is.kind = IPRewriterInputIMP::i_pattern;

    } else if (Element *e = cp_element(word, this, 0)) {
	IPMapperIMP *mapper = (IPMapperIMP *)e->cast("IPMapperIMP");
	if (rest)
	    return cerrh.error("syntax error, expected element name");
	else if (!mapper)
	    return cerrh.error("element is not an IPMapperIMP");
	else {
	    is.kind = IPRewriterInputIMP::i_mapper;
	    is.u.mapper = mapper;
	}

    } else
	return cerrh.error("unknown specification");

    return 0;
}

int
IPRewriterBaseIMP::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String capacity_word;
    uint32_t timeouts[2];
    bool has_timeout[2] = {false,false};
    int32_t heapcap;

    if (Args(this, errh).bind(conf)
	.read("CAPACITY", AnyArg(), capacity_word)
	.read("MAPPING_CAPACITY", AnyArg(), capacity_word)
	.read("TIMEOUT", SecondsArg(), timeouts[0]).read_status(has_timeout[0])
	.read("GUARANTEE", SecondsArg(), timeouts[1]).read_status(has_timeout[1])
	.read("REAP_INTERVAL", SecondsArg(), _gc_interval_sec)
	.read("REAP_TIME", Args::deprecated, SecondsArg(), _gc_interval_sec)
	.read("SET_AGGREGATE", _set_aggregate)
	.consume() < 0)
	return -1;


    for (unsigned i=0; i<_state.weight(); i++) {
        if (has_timeout[0])
        	initialize_timeout(0, timeouts[0]);
        if (has_timeout[1])
        	initialize_timeout(0, timeouts[1]);
    }

    if (capacity_word) {
        Element *e;
        IPRewriterBaseIMP *rwb;
        if (IntArg().parse(capacity_word, heapcap)) {
            for (unsigned i = 0; i < _state.weight(); i++) {
                _state.get_value(i).heap->_capacity = heapcap;
            }
        } else if ((e = cp_element(capacity_word, this))
             && (rwb = (IPRewriterBaseIMP *) e->cast("IPRewriterBase"))) {
            for (unsigned i = 0; i < _state.weight(); i++) {
                rwb->_state.get_value(i).heap->use();
                _state.get_value(i).heap->unuse();
                _state.get_value(i).heap = rwb->_state.get_value(i).heap;
            }
        } else
            return errh->error("bad MAPPING_CAPACITY");
    }



    if (conf.size() != ninputs())
	return errh->error("need %d arguments, one per input port", ninputs());

    for (unsigned i=0; i<_state.weight(); i++) {
        _state.get_value(i).timeouts[0] *= CLICK_HZ;    // _timeouts is measured in jiffies
        _state.get_value(i).timeouts[1] *= CLICK_HZ;
    }

    for (int i = 0; i < conf.size(); ++i) {
	IPRewriterInputIMP is;
	if (parse_input_spec(conf[i], is, i, errh) >= 0)
	    _input_specs.push_back(is);
    }

    return input_specs_size() == ninputs() ? 0 : -1;
}

int
IPRewriterBaseIMP::initialize(ErrorHandler *errh)
{
    for (int i = 0; i < input_specs_size(); ++i) {
	PrefixErrorHandler cerrh(errh, "input spec " + String(i) + ": ");
	/*if (input_specs(i)->reply_element()->_state._heap != _heap)
	    cerrh.error("reply element %<%s%> must share this MAPPING_CAPACITY", i, _input_specs[i].reply_element()->name().c_str());*/
	if (_input_specs[i].kind == IPRewriterInputIMP::i_mapper)
	    _input_specs[i].u.mapper_imp->notify_rewriter(this, &_input_specs[i], &cerrh);
    }
    for (unsigned i = 0; i < _state.weight(); i ++) {
        Timer& gc_timer = _state.get_value(i).gc_timer;
        new(&gc_timer) Timer(gc_timer_hook, this); //Reconstruct as Timer does not allow assignment
        gc_timer.initialize(this);
        gc_timer.move_thread(_state.get_mapping(i));
        if (_gc_interval_sec)
            gc_timer.schedule_after_sec(_gc_interval_sec);
    }
    return errh->nerrors() ? -1 : 0;
}

void
IPRewriterBaseIMP::cleanup(CleanupStage)
{
    for (unsigned i = 0; i < _state.weight(); i++)
        shrink_heap(true, i);
    for (int i = 0; i < input_specs_size(); ++i)
	if (_input_specs[i].kind == IPRewriterInputIMP::i_pattern)
	    _input_specs[i].u.pattern->unuse();
    _input_specs.clear();
}

IPRewriterEntry *
IPRewriterBaseIMP::get_entry(int ip_p, const IPFlowID &flowid, int input)
{
    IPRewriterEntry *m = map().get(flowid);
    if (m && ip_p && m->flow()->ip_p() && m->flow()->ip_p() != ip_p)
	return 0;
    if (!m && (unsigned) input < (unsigned) input_specs_size()) {
	IPRewriterInputIMP &is = input_specs(input);
	IPFlowID rewritten_flowid = IPFlowID::uninitialized_t();
	if (is.rewrite_flowid(flowid, rewritten_flowid, 0) == rw_addmap)
	    m = add_flow(ip_p, flowid, rewritten_flowid, input);
    }
    return m;
}

IPRewriterEntry *
IPRewriterBaseIMP::store_flow(IPRewriterFlow *flow, int input,
			   Map &map, Map *reply_map_ptr)
{
    IPRewriterBaseIMP *reply_element = input_specs(input).reply_element();
    if ((unsigned) flow->entry(false).output() >= (unsigned) noutputs()
	|| (unsigned) flow->entry(true).output() >= (unsigned) reply_element->noutputs()) {
	flow->ownerimp()->owner->destroy_flow(flow);
	return 0;
    }

    IPRewriterEntry *old = map.set(&flow->entry(false));
    assert(!old);


    if (!reply_map_ptr)
	reply_map_ptr = &reply_element->map();
    old = reply_map_ptr->set(&flow->entry(true));
    if (unlikely(old)) {		// Assume every map has the same heap.
	if (likely(old->flow() != flow))
	    old->flowimp()->destroy(heap());
    }

    Vector<IPRewriterFlow *> &myheap = heap()->_heaps[flow->guaranteed()];
    myheap.push_back(flow);
    push_heap(myheap.begin(), myheap.end(),
	      IPRewriterFlow::heap_less(), IPRewriterFlow::heap_place());
    ++input_specs(input).count;

    if (unlikely(heap()->size() > heap()->capacity())) {
	// This may destroy the newly added mapping, if it has the lowest
	// expiration time.  How can we tell?  If (1) flows are added to the
	// heap one at a time, so the heap was formerly no bigger than the
	// capacity, and (2) 'flow' expires in the future, then we will only
	// destroy 'flow' if it's the top of the heap.
	click_jiffies_t now_j = click_jiffies();
	assert(click_jiffies_less(now_j, flow->expiry())
	       && heap()->size() == heap()->capacity() + 1);
	if (shrink_heap_for_new_flow(flow, now_j)) {
	    ++input_specs(input).failures;
	    return 0;
	}
    }

    if (map.unbalanced())
	map.rehash(map.bucket_count() + 1);
    if (reply_map_ptr != &map && reply_map_ptr->unbalanced())
	reply_map_ptr->rehash(reply_map_ptr->bucket_count() + 1);
    return &flow->entry(false);
}

void
IPRewriterBaseIMP::shift_heap_best_effort(click_jiffies_t now_j)
{
    // Shift flows with expired guarantees to the best-effort heap.
    Vector<IPRewriterFlow *> &guaranteed_heap =heap()->_heaps[1];
    while (guaranteed_heap.size() && guaranteed_heap[0]->expired(now_j)) {
	IPRewriterFlow *mf = guaranteed_heap[0];
	click_jiffies_t new_expiry = mf->ownerimp()->owner->best_effort_expiry((IPRewriterFlow*)mf);
	mf->change_expiry(heap(), false, new_expiry);
    }
}

bool
IPRewriterBaseIMP::shrink_heap_for_new_flow(IPRewriterFlow *flow,
					 click_jiffies_t now_j)
{
    shift_heap_best_effort(now_j);
    // At this point, all flows in the guarantee heap expire in the future.
    // So remove the next-to-expire best-effort flow, unless there are none.
    // In that case we always remove the current flow to honor previous
    // guarantees (= admission control).
    IPRewriterFlow *deadf;
    if (heap()->_heaps[0].empty()) {
	assert(flow->guaranteed());
	deadf = flow;
    } else
	deadf =heap()->_heaps[0][0];
    deadf->destroy(heap());
    return deadf == flow;
}

void
IPRewriterBaseIMP::shrink_heap(bool clear_all, int thid)
{
    click_jiffies_t now_j = click_jiffies();
    shift_heap_best_effort(now_j);
    Vector<IPRewriterFlow *> &best_effort_heap = _state.get_value(thid).heap->_heaps[0];
    while (best_effort_heap.size() && best_effort_heap[0]->expired(now_j))
	best_effort_heap[0]->destroy(_state.get_value(thid).heap);

    int32_t capacity = clear_all ? 0 : _state.get_value(thid).heap->_capacity;
    while (_state.get_value(thid).heap->size() > capacity) {
	IPRewriterFlow *deadf = _state.get_value(thid).heap->_heaps[_state.get_value(thid).heap->_heaps[0].empty()][0];
	deadf->destroy(_state.get_value(thid).heap);
    }
}

void
IPRewriterBaseIMP::dump_mappings(StringAccum& sa) {
	click_jiffies_t now = click_jiffies();
	for (int i = 0; i < mem_units_no(); i++) {
		for (Map::iterator iter = _state.get_value(i).map.begin(); iter.live(); ++iter) {
			iter->flowimp()->unparse(sa, iter->direction(), now);
			sa << '\n';
		}
	}
}

void
IPRewriterBaseIMP::gc_timer_hook(Timer *t, void *user_data)
{
    IPRewriterBaseIMP *rw = static_cast<IPRewriterBaseIMP *>(user_data);
    rw->shrink_heap(false, click_current_cpu_id());
    if (rw->_gc_interval_sec)
        t->reschedule_after_sec(rw->_gc_interval_sec);
}

String
IPRewriterBaseIMP::read_handler(Element *e, void *user_data)
{
    IPRewriterBaseIMP *rw = static_cast<IPRewriterBaseIMP *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    StringAccum sa;

    switch (what) {
    case h_nmappings: {
	uint32_t count = 0;
	for (int i = 0; i < rw->input_specs_size(); ++i)
	    count += rw->_input_specs[i].count;
	sa << count;
	break;
    }
    case h_mapping_failures: {
	uint32_t count = 0;
	for (int i = 0; i < rw->input_specs_size(); ++i)
	    count += rw->_input_specs[i].failures;
	sa << count;
	break;
    }
    case h_size:
	sa << rw->heap()->size(); //TODO : In mt context, we must pass the sum as the handler is called by one "random" thread
	break;
    case h_capacity:
	sa << rw->heap()->_capacity;
	break;
    default:
	for (int i = 0; i < rw->input_specs_size(); ++i) {
	    if (what != h_patterns && what != i)
		continue;
	    switch (rw->_input_specs[i].kind) {
	    case IPRewriterInputIMP::i_drop:
		sa << "<drop>";
		break;
	    case IPRewriterInputIMP::i_nochange:
		sa << "<nochange>";
		break;
	    case IPRewriterInputIMP::i_keep:
		sa << "<keep>";
		break;
	    case IPRewriterInputIMP::i_pattern:
		sa << rw->_input_specs[i].u.pattern->unparse();
		break;
	    case IPRewriterInputIMP::i_mapper:
		sa << "<mapper>";
		break;
	    }
	    if (rw->_input_specs[i].count)
		sa << " [" << rw->_input_specs[i].count << ']';
	    sa << '\n';
	}
	break;
    }
    return sa.take_string();
}

int
IPRewriterBaseIMP::write_handler(const String &str, Element *e, void *user_data, ErrorHandler *errh)
{
    IPRewriterBaseIMP *rw = static_cast<IPRewriterBaseIMP *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    if (what == h_capacity) {

    assert(click_current_cpu_id() == 0); //MT to be reviewed
	if (Args(e, errh).push_back_words(str)
	    .read_mp("CAPACITY", rw->heap()->_capacity) //TODO : Same comments about MT
	    .complete() < 0)
	    return -1;
	rw->shrink_heap(false, click_current_cpu_id());
	return 0;
    } else if (what == h_clear) {
	rw->shrink_heap(true, click_current_cpu_id());
	return 0;
    } else
	return -1;
}

int
IPRewriterBaseIMP::pattern_write_handler(const String &str, Element *e, void *user_data, ErrorHandler *errh)
{
    IPRewriterBaseIMP *rw = static_cast<IPRewriterBaseIMP *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    IPRewriterInputIMP is;
    int r = rw->parse_input_spec(str, is, what, errh);
    if (r >= 0) {
    	IPRewriterInputIMP *spec = (IPRewriterInputIMP*)&rw->_input_specs[what];

    assert(click_current_cpu_id() == 0); //MT to be reviewed

	// remove all existing flows created by this input
	for (int which_heap = 0; which_heap < 2; ++which_heap) {
	    Vector<IPRewriterFlow *> &myheap = rw->heap()->_heaps[which_heap]; //TODO : Same comment about MT
	    for (int i = myheap.size() - 1; i >= 0; --i)
		if (myheap[i]->ownerimp() == spec) {
		    myheap[i]->destroy(rw->heap());  //TODO : Same comment about MT
		    if (i < myheap.size())
			++i;
		}
	}

	// change pattern
	if (spec->kind == IPRewriterInputIMP::i_pattern)
	    spec->u.pattern->unuse(); //TODO : Ensure MT safeness
	*spec = is;
    }
    return 0;
}

void
IPRewriterBaseIMP::add_rewriter_handlers(bool writable_patterns)
{
    add_read_handler("table_size", read_handler, h_nmappings);
    add_read_handler("nmappings", read_handler, h_nmappings, Handler::h_deprecated);
    add_read_handler("mapping_failures", read_handler, h_mapping_failures);
    add_read_handler("patterns", read_handler, h_patterns);
    add_read_handler("size", read_handler, h_size);
    add_read_handler("capacity", read_handler, h_capacity);
    add_write_handler("capacity", write_handler, h_capacity);
    add_write_handler("clear", write_handler, h_clear);
    for (int i = 0; i < ninputs(); ++i) {
	String name = "pattern" + String(i);
	add_read_handler(name, read_handler, i);
	if (writable_patterns)
	    add_write_handler(name, pattern_write_handler, i);
    }
}

int
IPRewriterBaseIMP::llrpc(unsigned command, void *data)
{
    if (command == CLICK_LLRPC_IPREWRITER_MAP_TCP) {
	// Data	: unsigned saddr, daddr; unsigned short sport, dport
	// Incoming : the flow ID
	// Outgoing : If there is a mapping for that flow ID, then stores the
	//	      mapping into 'data' and returns zero. Otherwise, returns
	//	      -EAGAIN.

	IPFlowID *val = reinterpret_cast<IPFlowID *>(data);
	IPRewriterEntry *m = get_entry(IP_PROTO_TCP, *val, -1);
	if (!m)
	    return -EAGAIN;
	*val = m->rewritten_flowid();
	return 0;

    } else if (command == CLICK_LLRPC_IPREWRITER_MAP_UDP) {
	// Data	: unsigned saddr, daddr; unsigned short sport, dport
	// Incoming : the flow ID
	// Outgoing : If there is a mapping for that flow ID, then stores the
	//	      mapping into 'data' and returns zero. Otherwise, returns
	//	      -EAGAIN.

	IPFlowID *val = reinterpret_cast<IPFlowID *>(data);
	IPRewriterEntry *m = get_entry(IP_PROTO_UDP, *val, -1);
	if (!m)
	    return -EAGAIN;
	*val = m->rewritten_flowid();
	return 0;

    } else
	return Element::llrpc(command, data);
}

ELEMENT_REQUIRES(IPRewriterMapping IPRewriterPattern)
ELEMENT_PROVIDES(IPRewriterBaseIMP)
CLICK_ENDDECLS
