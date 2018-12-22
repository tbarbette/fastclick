/*
 * threadipmapper.{cc,hh} -- thread IPMapper
 * Tom Barbette
 *
 * Copyright (c) 2018 KTH Royal Institute of Technology
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
#include "threadipmapper.hh"
#include "elements/ip/iprwpattern.hh"
#include <click/confparse.hh>
#include <click/error.hh>
CLICK_DECLS

ThreadIPMapper::ThreadIPMapper()
{
}

ThreadIPMapper::~ThreadIPMapper()
{
}

void *
ThreadIPMapper::cast(const char *name)
{
  if (name && strcmp("ThreadIPMapper", name) == 0)
    return (Element *)this;
  else if (name && strcmp("IPMapper", name) == 0)
    return (IPMapper *)this;
  else
    return 0;
}

int
ThreadIPMapper::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (conf.size() == 0)
	return errh->error("no patterns given");
    else if (conf.size() == 1)
	errh->warning("only one pattern given");

    for (int i = 0; i < conf.size(); i++) {
	IPRewriterInput is;
	is.kind = IPRewriterInput::i_pattern;
	if (IPRewriterPattern::parse_with_ports(conf[i], &is, this, errh)) {
	    is.u.pattern->use();
	    _is.push_back(is);
	}
    }


    return errh->nerrors() ? -1 : 0;
}

void
ThreadIPMapper::cleanup(CleanupStage)
{
    for (int i = 0; i < _is.size(); i++)
	_is[i].u.pattern->unuse();
}

void
ThreadIPMapper::notify_rewriter(IPRewriterBase *user,
				    IPRewriterInput *input, ErrorHandler *errh)
{
    for (int i = 0; i < _is.size(); i++) {
	if (_is[i].foutput >= user->noutputs()
	    || _is[i].routput >= input->reply_element->noutputs())
	    errh->error("output port out of range in %s pattern %d", declaration().c_str(), i);
    }
}

int
ThreadIPMapper::rewrite_flowid(IPRewriterInput *input,
				   const IPFlowID &flowid,
				   IPFlowID &rewritten_flowid,
				   Packet *p, int mapid)
{

    int i = click_current_cpu_id();
	IPRewriterInput &is = _is[i];
	is.reply_element = input->reply_element;
	int result = is.rewrite_flowid(flowid, rewritten_flowid, p, mapid);
	if (result != IPRewriterBase::rw_drop
	    || is.kind == IPRewriterInput::i_drop) {
	    input->foutput = is.foutput;
	    input->routput = is.routput;
	    return result;
	}
    return IPRewriterBase::rw_drop;
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(IPRewriterBase)
EXPORT_ELEMENT(ThreadIPMapper)
