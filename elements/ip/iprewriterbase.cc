// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * iprewriterbase.{cc,hh} -- rewrites packet source and destination
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
#include "iprewriterbase.hh"
#include "elements/ip/iprwpatterns.hh"
#include "elements/ip/iprwmapping.hh"
#include "elements/ip/iprwpattern.hh"
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
#include <click/llrpc.h>
#include <click/args.hh>
#include <click/straccum.hh>
#include <click/error.hh>
#include <click/algorithm.hh>
#include <click/heap.hh>

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
// IPMapper
//

void
IPMapper::notify_rewriter(IPRewriterBase *, IPRewriterInput *, ErrorHandler *)
{
}

int
IPMapper::rewrite_flowid(IPRewriterInput *, const IPFlowID &, IPFlowID &,
			 Packet *, int)
{
    return IPRewriterBase::rw_drop;
}

//
// IPRewriterBase
//

IPRewriterBase::IPRewriterBase()
    : _state(), _set_aggregate(false), _handle_migration(false)
{
    _gc_interval_sec = default_gc_interval;

    _mem_units_no = (click_max_cpu_ids() == 0)? 1 : click_max_cpu_ids();

    // One heap and map per core
    _heap = new IPRewriterHeap*[_mem_units_no];
    _timeouts  = new uint32_t*[_mem_units_no];
    for (unsigned i=0; i<_mem_units_no; i++) {
        _heap[i] = new IPRewriterHeap;
        _timeouts[i] = new uint32_t[2];
        _timeouts[i][0] = default_timeout;
        _timeouts[i][1] = default_guarantee;
    }
}

IPRewriterBase::~IPRewriterBase()
{
    if (_heap) {
        for (unsigned i=0; i<_mem_units_no; i++) {
            _heap[i]->unuse();
        }
        delete [] _heap;
    }

    if (_timeouts) {
        for (unsigned i=0; i<_mem_units_no; i++) {
            delete _timeouts[i];
        }
        delete [] _timeouts;
    }
}


int
IPRewriterBase::parse_input_spec(const String &line, IPRewriterInput &is,
				 int input_number, ErrorHandler *errh)
{
    PrefixErrorHandler cerrh(errh, "input spec " + String(input_number) + ": ");
    String word, rest;
    if (!cp_word(line, &word, &rest))
	return cerrh.error("empty argument");
    cp_eat_space(rest);

    is.kind = IPRewriterInput::i_drop;
    is.owner = this;
    is.owner_input = input_number;
    is.reply_element = this;

    if (word == "pass" || word == "passthrough" || word == "nochange") {
	int32_t outnum = 0;
	if (rest && !IntArg().parse(rest, outnum))
	    return cerrh.error("syntax error, expected %<nochange [OUTPUT]%>");
	else if ((unsigned) outnum >= (unsigned) noutputs())
	    return cerrh.error("output port out of range");
	is.kind = IPRewriterInput::i_nochange;
	is.foutput = outnum;

    } else if (word == "keep") {
	Vector<String> words;
	cp_spacevec(rest, words);
	if (!IPRewriterPattern::parse_ports(words, &is, this, &cerrh))
	    return -1;
	if ((unsigned) is.foutput >= (unsigned) noutputs()
	    || (unsigned) is.routput >= (unsigned) is.reply_element->noutputs())
	    return cerrh.error("output port out of range");
	is.kind = IPRewriterInput::i_keep;

    } else if (word == "drop" || word == "discard") {
	if (rest)
	    return cerrh.error("syntax error, expected %<%s%>", word.c_str());

    } else if (word == "pattern" || word == "xpattern") {
	if (!IPRewriterPattern::parse_with_ports(rest, &is, this, &cerrh))
	    return -1;
	if ((unsigned) is.foutput >= (unsigned) noutputs()
	    || (unsigned) is.routput >= (unsigned) is.reply_element->noutputs())
	    return cerrh.error("output port out of range");
	is.u.pattern->use();
	is.kind = IPRewriterInput::i_pattern;

    } else if (Element *e = cp_element(word, this, 0)) {
	IPMapper *mapper = (IPMapper *)e->cast("IPMapper");
	if (rest)
	    return cerrh.error("syntax error, expected element name");
	else if (!mapper)
	    return cerrh.error("element is not an IPMapper");
	else {
	    is.kind = IPRewriterInput::i_mapper;
	    is.u.mapper = mapper;
	}

    } else
	return cerrh.error("unknown specification");

    return 0;
}

int
IPRewriterBase::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String capacity_word;
    uint32_t timeouts[2];
    bool has_timeout[2] = {false,false};
    int32_t heapcap;
    bool use_cache = false;
    bool set_aggregate = false;
    bool _handle_migration; //TODO Temp placeholder

    if (Args(this, errh).bind(conf)
	.read("CAPACITY", AnyArg(), capacity_word)
	.read("MAPPING_CAPACITY", AnyArg(), capacity_word)
	.read("TIMEOUT", SecondsArg(), timeouts[0]).read_status(has_timeout[0])
	.read("GUARANTEE", SecondsArg(), timeouts[1]).read_status(has_timeout[1])
	.read("REAP_INTERVAL", SecondsArg(), _gc_interval_sec)
	.read("REAP_TIME", Args::deprecated, SecondsArg(), _gc_interval_sec)
	.read("USE_CACHE", use_cache)
	.read("SET_AGGREGATE", set_aggregate)
	.read("HANDLE_MIGRATION", _handle_migration)
	.consume() < 0)
	return -1;


    for (unsigned i=0; i<_mem_units_no; i++) {
        if (has_timeout[0])
            _timeouts[i][0] = timeouts[0];
        if (has_timeout[1])
            _timeouts[i][1] = timeouts[1];
    }

    _use_cache = use_cache;
    _set_aggregate = set_aggregate;

    if (capacity_word) {
        Element *e;
        IPRewriterBase *rwb;
        if (IntArg().parse(capacity_word, heapcap)) {
            for (unsigned i = 0; i < _mem_units_no; i++) {
                _heap[i]->_capacity = heapcap;
            }
        } else if ((e = cp_element(capacity_word, this))
             && (rwb = (IPRewriterBase *) e->cast("IPRewriterBase"))) {
            for (unsigned i = 0; i < _mem_units_no; i++) {
                rwb->_heap[i]->use();
                _heap[i]->unuse();
                _heap[i] = rwb->_heap[i];
            }
        } else
            return errh->error("bad MAPPING_CAPACITY");
    }



    if (conf.size() != ninputs())
	return errh->error("need %d arguments, one per input port", ninputs());

    for (unsigned i=0; i<_mem_units_no; i++) {
        _timeouts[i][0] *= CLICK_HZ;    // _timeouts is measured in jiffies
        _timeouts[i][1] *= CLICK_HZ;
    }

    for (int i = 0; i < conf.size(); ++i) {
	IPRewriterInput is;
	if (parse_input_spec(conf[i], is, i, errh) >= 0)
	    _input_specs.push_back(is);
    }

    return _input_specs.size() == ninputs() ? 0 : -1;
}

int
IPRewriterBase::initialize(ErrorHandler *errh)
{
    for (int i = 0; i < _input_specs.size(); ++i) {
	PrefixErrorHandler cerrh(errh, "input spec " + String(i) + ": ");
	if (_input_specs[i].reply_element->_heap != _heap)
	    cerrh.error("reply element %<%s%> must share this MAPPING_CAPACITY", i, _input_specs[i].reply_element->name().c_str());
	if (_input_specs[i].kind == IPRewriterInput::i_mapper)
	    _input_specs[i].u.mapper->notify_rewriter(this, &_input_specs[i], &cerrh);
    }

    for (int i = 0; i < _state.weight(); i ++) {
        IPRewriterState &state = _state.get_value(i);
        Timer& gc_timer = state.gc_timer;
        new(&gc_timer) Timer(gc_timer_hook, this); //Reconstruct as Timer does not allow assignment
        gc_timer.initialize(this);
        gc_timer.move_thread(_state.get_mapping(i));
        if (_gc_interval_sec)
            gc_timer.schedule_after_sec(_gc_interval_sec);
    }
    return errh->nerrors() ? -1 : 0;
}

void
IPRewriterBase::cleanup(CleanupStage)
{
    for (unsigned i = 0; i < _mem_units_no; i++)
        shrink_heap(true, i);
    for (int i = 0; i < _input_specs.size(); ++i)
	if (_input_specs[i].kind == IPRewriterInput::i_pattern)
	    _input_specs[i].u.pattern->unuse();
    _input_specs.clear();
}

IPRewriterEntry *
IPRewriterBase::get_entry(int ip_p, const IPFlowID &flowid, int input)
{
    //We do not need to get a reference because we are the only writer
    IPRewriterEntry *m = search_entry(flowid);


    if (m && ip_p && m->flow()->ip_p() && m->flow()->ip_p() != ip_p)
	return 0;
    if (!m && (unsigned) input < (unsigned) _input_specs.size()) {

	IPFlowID rewritten_flowid;

        if (_handle_migration && !precopy)
            m = search_migrate_entry(flowid, _state);

        if (m) {
            rewritten_flowid = m->rewritten_flowid();
        } else {
	        IPRewriterInput &is = _input_specs[input];
            rewritten_flowid = IPFlowID::uninitialized_t();
	        if (!(is.rewrite_flowid(flowid, rewritten_flowid, 0) == rw_addmap)) {
                return 0;
            }
        }
	    m = add_flow(ip_p, flowid, rewritten_flowid, input);
    }
    return m;
}

IPRewriterEntry *
IPRewriterBase::store_flow(IPRewriterFlow *flow, int input,
			   Map &map, Map *reply_map_ptr)
{
    IPRewriterState &state = *_state;
    IPRewriterBase *reply_element = _input_specs[input].reply_element;
    if ((unsigned) flow->entry(false).output() >= (unsigned) noutputs()
	|| (unsigned) flow->entry(true).output() >= (unsigned) reply_element->noutputs()) {
	    flow->owner()->owner->destroy_flow(flow);
	    return 0;
    }

	state.map_lock.write_begin();
    IPRewriterEntry *old = map.set(&flow->entry(false));
	state.map_lock.write_end();
	if (old) {
		if (_handle_migration)
			return old; //TODO : an old flow is back. Change expiry
		else
			assert(!old);
	}

    auto &heap = _heap[click_current_cpu_id()];

    if (!reply_map_ptr)
	reply_map_ptr = &reply_element->_state->map;
    old = reply_map_ptr->set(&flow->entry(true));
    if (unlikely(old)) {		// Assume every map has the same heap.
	if (likely(old->flow() != flow))
		old->flow()->destroy(heap);
    }

    Vector<IPRewriterFlow *> &myheap = heap->_heaps[flow->guaranteed()];
    myheap.push_back(flow);
    push_heap(myheap.begin(), myheap.end(),
	      IPRewriterFlow::heap_less(), IPRewriterFlow::heap_place());
    ++_input_specs[input].count;

    if (unlikely(heap->size() > heap->capacity())) {
	// This may destroy the newly added mapping, if it has the lowest
	// expiration time.  How can we tell?  If (1) flows are added to the
	// heap one at a time, so the heap was formerly no bigger than the
	// capacity, and (2) 'flow' expires in the future, then we will only
	// destroy 'flow' if it's the top of the heap.
	click_jiffies_t now_j = click_jiffies();
	assert(click_jiffies_less(now_j, flow->expiry())
	       && heap->size() == heap->capacity() + 1);
	if (shrink_heap_for_new_flow(flow, now_j)) {
	    ++_input_specs[input].failures;
	    return 0;
	}
    }

    if (map.unbalanced()) {
              state.map_lock.write_begin();
              map.rehash(map.bucket_count() + 1);
              state.map_lock.write_end();
    }
    if (reply_map_ptr != &map && reply_map_ptr->unbalanced()) {
              state.map_lock.write_begin();
          reply_map_ptr->rehash(reply_map_ptr->bucket_count() + 1);
              state.map_lock.write_begin();
    }

    return &flow->entry(false);
}

void
IPRewriterBase::shift_heap_best_effort(click_jiffies_t now_j)
{
    // Shift flows with expired guarantees to the best-effort heap.
    Vector<IPRewriterFlow *> &guaranteed_heap = _heap[click_current_cpu_id()]->_heaps[1];
    while (guaranteed_heap.size() && guaranteed_heap[0]->expired(now_j)) {
	IPRewriterFlow *mf = guaranteed_heap[0];
	click_jiffies_t new_expiry = mf->owner()->owner->best_effort_expiry(mf);
	mf->change_expiry(_heap[click_current_cpu_id()], false, new_expiry);
    }
}

int IPRewriterBase::thread_configure(ThreadReconfigurationStage stage, ErrorHandler* errh, Bitvector threads) {
	if (stage == THREAD_RECONFIGURE_UP_PRE) {
        set_migration(true, threads, _state);
	} else if (stage == THREAD_RECONFIGURE_DOWN_PRE){
        set_migration(false, threads, _state);
	}
	return 0;
}

bool
IPRewriterBase::shrink_heap_for_new_flow(IPRewriterFlow *flow,
					 click_jiffies_t now_j)
{
    shift_heap_best_effort(now_j);
    // At this point, all flows in the guarantee heap expire in the future.
    // So remove the next-to-expire best-effort flow, unless there are none.
    // In that case we always remove the current flow to honor previous
    // guarantees (= admission control).
    IPRewriterFlow *deadf;
    if (_heap[click_current_cpu_id()]->_heaps[0].empty()) {
	assert(flow->guaranteed());
	deadf = flow;
    } else
	deadf = _heap[click_current_cpu_id()]->_heaps[0][0];
    deadf->destroy(_heap[click_current_cpu_id()]);
    return deadf == flow;
}

void
IPRewriterBase::shrink_heap(bool clear_all, int thid)
{
    click_jiffies_t now_j = click_jiffies();
    shift_heap_best_effort(now_j);
    Vector<IPRewriterFlow *> &best_effort_heap = _heap[thid]->_heaps[0];
    while (best_effort_heap.size() && best_effort_heap[0]->expired(now_j))
	best_effort_heap[0]->destroy(_heap[thid]);

    int32_t capacity = clear_all ? 0 : _heap[thid]->_capacity;
    while (_heap[thid]->size() > capacity) {
	IPRewriterFlow *deadf = _heap[thid]->_heaps[_heap[thid]->_heaps[0].empty()][0];
	deadf->destroy(_heap[thid]);
    }
}

void
IPRewriterBase::gc_timer_hook(Timer *t, void *user_data)
{
    IPRewriterBase *rw = static_cast<IPRewriterBase *>(user_data);
    rw->shrink_heap(false, click_current_cpu_id());
    if (rw->_gc_interval_sec)
        t->reschedule_after_sec(rw->_gc_interval_sec);
}

String
IPRewriterBase::read_handler(Element *e, void *user_data)
{
    IPRewriterBase *rw = static_cast<IPRewriterBase *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    StringAccum sa;

    switch (what) {
    case h_nmappings: {
	uint32_t count = 0;
	for (int i = 0; i < rw->_input_specs.size(); ++i)
	    count += rw->_input_specs[i].count;
	sa << count;
	break;
    }
    case h_mapping_failures: {
	uint32_t count = 0;
	for (int i = 0; i < rw->_input_specs.size(); ++i)
	    count += rw->_input_specs[i].failures;
	sa << count;
	break;
    }
    case h_size:
	sa << rw->_heap[click_current_cpu_id()]->size(); //TODO : In mt context, we must pass the sum as the handler is called by one "random" thread
	break;
    case h_capacity:
	sa << rw->_heap[click_current_cpu_id()]->_capacity;
	break;
    default:
	for (int i = 0; i < rw->_input_specs.size(); ++i) {
	    if (what != h_patterns && what != i)
		continue;
	    switch (rw->_input_specs[i].kind) {
	    case IPRewriterInput::i_drop:
		sa << "<drop>";
		break;
	    case IPRewriterInput::i_nochange:
		sa << "<nochange>";
		break;
	    case IPRewriterInput::i_keep:
		sa << "<keep>";
		break;
	    case IPRewriterInput::i_pattern:
		sa << rw->_input_specs[i].u.pattern->unparse();
		break;
	    case IPRewriterInput::i_mapper:
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
IPRewriterBase::write_handler(const String &str, Element *e, void *user_data, ErrorHandler *errh)
{
    IPRewriterBase *rw = static_cast<IPRewriterBase *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    if (what == h_capacity) {

    assert(click_current_cpu_id() == 0); //MT to be reviewed
	if (Args(e, errh).push_back_words(str)
	    .read_mp("CAPACITY", rw->_heap[click_current_cpu_id()]->_capacity) //TODO : Same comments about MT
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
IPRewriterBase::pattern_write_handler(const String &str, Element *e, void *user_data, ErrorHandler *errh)
{
    IPRewriterBase *rw = static_cast<IPRewriterBase *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    IPRewriterInput is;
    int r = rw->parse_input_spec(str, is, what, errh);
    if (r >= 0) {
	IPRewriterInput *spec = &rw->_input_specs[what];

    assert(click_current_cpu_id() == 0); //MT to be reviewed

	// remove all existing flows created by this input
	for (int which_heap = 0; which_heap < 2; ++which_heap) {
	    Vector<IPRewriterFlow *> &myheap = rw->_heap[click_current_cpu_id()]->_heaps[which_heap]; //TODO : Same comment about MT
	    for (int i = myheap.size() - 1; i >= 0; --i)
		if (myheap[i]->owner() == spec) {
		    myheap[i]->destroy(rw->_heap[click_current_cpu_id()]);  //TODO : Same comment about MT
		    if (i < myheap.size())
			++i;
		}
	}

	// change pattern
	if (spec->kind == IPRewriterInput::i_pattern)
	    spec->u.pattern->unuse(); //TODO : Ensure MT safeness
	*spec = is;
    }
    return 0;
}

void
IPRewriterBase::add_rewriter_handlers(bool writable_patterns)
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
IPRewriterBase::llrpc(unsigned command, void *data)
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
ELEMENT_PROVIDES(IPRewriterBase)
CLICK_ENDDECLS
