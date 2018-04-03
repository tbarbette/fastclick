/*
 * rripmapper.{cc,hh} -- round robin IPMapper
 * Eddie Kohler
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2009-2010 Meraki, Inc.
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
#include "rripmapper.hh"
#include <click/confparse.hh>
#include <click/error.hh>
#include "iprwpattern.hh"
CLICK_DECLS

RoundRobinIPMapper::RoundRobinIPMapper()
{
}

RoundRobinIPMapper::~RoundRobinIPMapper()
{
}

void *
RoundRobinIPMapper::cast(const char *name)
{
  if (name && strcmp("RoundRobinIPMapper", name) == 0)
    return (Element *)this;
  else if (name && strcmp("IPMapper", name) == 0)
    return (IPMapper *)this;
  else
    return 0;
}

int
RoundRobinIPMapperBase::configure(Vector<String> &conf, ErrorHandler *errh)
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

    _last_pattern = 0;
    return errh->nerrors() ? -1 : 0;
}

void
RoundRobinIPMapperBase::cleanup(CleanupStage)
{
    for (int i = 0; i < _is.size(); i++)
	_is[i].u.pattern->unuse();
}


void
RoundRobinIPMapper::notify_rewriter(IPRewriterBaseAncestor *user,
				    IPRewriterInput *input, ErrorHandler *errh)
{
    for (int i = 0; i < _is.size(); i++) {
	if (_is[i].foutput >= user->noutputs()
	    || _is[i].routput >= input->reply_element()->noutputs())
	    errh->error("output port out of range in %s pattern %d", declaration().c_str(), i);
    }
}

int
RoundRobinIPMapper::rewrite_flowid(IPRewriterInputAncestor *input,
				   const IPFlowID &flowid,
				   IPFlowID &rewritten_flowid,
				   Packet *p, int mapid)
{
    for (int i = 0; i < _is.size(); ++i) {
	IPRewriterInput &is = _is[_last_pattern];
	++_last_pattern;
	if (_last_pattern == _is.size())
	    _last_pattern = 0;
	is.set_reply_element(((IPRewriterInput*)input)->reply_element());
	int result = is.rewrite_flowid(flowid, rewritten_flowid, p, mapid);
	if (result != IPRewriterBase::rw_drop
	    || is.kind == IPRewriterInput::i_drop) {
	    input->foutput = is.foutput;
	    input->routput = is.routput;
	    return result;
	}
    }
    return IPRewriterBase::rw_drop;
}


RoundRobinIPMapperIMP::RoundRobinIPMapperIMP()
{
}

RoundRobinIPMapperIMP::~RoundRobinIPMapperIMP()
{

}


void
RoundRobinIPMapperIMP::notify_rewriter(IPRewriterBaseAncestor *user,
				    IPRewriterInput *input_base, ErrorHandler *errh)
{
    IPRewriterInputIMP* input = (IPRewriterInputIMP*)input_base;
    for (int i = 0; i < _is.size(); i++) {
	if (_is[i].foutput >= user->noutputs()
	    || _is[i].routput >= input->reply_element()->noutputs())
	    errh->error("output port out of range in %s pattern %d", declaration().c_str(), i);
    }
}

void *
RoundRobinIPMapperIMP::cast(const char *name)
{
  if (name && strcmp("RoundRobinIPMapperIMP", name) == 0)
    return (Element *)this;
  else if (name && strcmp("IPMapperIMP", name) == 0)
    return (IPMapperIMP *)this;
  else
    return 0;
}


int
RoundRobinIPMapperIMP::rewrite_flowid(IPRewriterInputAncestor *input,
				   const IPFlowID &flowid,
				   IPFlowID &rewritten_flowid,
				   Packet *p, int mapid)
{
    for (int i = 0; i < _is.size(); ++i) {
	IPRewriterInputIMP &is = (IPRewriterInputIMP&)_is[_last_pattern];
	++_last_pattern;
	if (_last_pattern == _is.size())
	    _last_pattern = 0;
	is.set_reply_element(((IPRewriterInputIMP*)input)->reply_element());
	int result = is.rewrite_flowid(flowid, rewritten_flowid, p, mapid);
	if (result != IPRewriterBase::rw_drop
	    || is.kind == IPRewriterInput::i_drop) {
	    input->foutput = is.foutput;
	    input->routput = is.routput;
	    return result;
	}
    }
    return IPRewriterBase::rw_drop;
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(IPRewriterBase)
EXPORT_ELEMENT(RoundRobinIPMapper RoundRobinIPMapperIMP)
