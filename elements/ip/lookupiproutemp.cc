/*
 * lookupiproutemp.{cc,hh} -- element looks up next-hop address in static
 * routing table, has processor local cache in SMP mode.
 * Robert Morris
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
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
#include "lookupiproutemp.hh"
#include <click/ipaddress.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>

#ifdef CLICK_LINUXMODULE
# include <click/cxxprotect.h>
 CLICK_CXX_PROTECT
# include <linux/sched.h>
 CLICK_CXX_UNPROTECT
# include <click/cxxunprotect.h>
#endif

CLICK_DECLS

LookupIPRouteMP::LookupIPRouteMP()
{
#if HAVE_USER_MULTITHREAD
    _cache = 0;
#endif
}

LookupIPRouteMP::~LookupIPRouteMP()
{
}

int
LookupIPRouteMP::configure(Vector<String> &conf, ErrorHandler *errh)
{
  int maxout = -1;
  _t.clear();

  for (int i = 0; i < conf.size(); i++) {
    IPAddress dst, mask, gw;
    int32_t output_num = 0;
    bool ok = false;

    Vector<String> words;
    cp_spacevec(conf[i], words);

    if ((words.size() == 2 || words.size() == 3)
	&& IPPrefixArg(true).parse(words[0], dst, mask, this) // allow base IP addresses
	&& IntArg().parse(words.back(), output_num)) {
      if (words.size() == 3)
	  ok = IPAddressArg().parse(words[1], gw, this);
      else
	ok = true;
    }

    if (ok && output_num >= 0) {
      _t.add(dst, mask, gw, output_num);
      if (output_num > maxout)
        maxout = output_num;
    } else
      errh->error("argument %d should be `DADDR/MASK [GATEWAY] OUTPUT'", i+1);
  }
  if (errh->nerrors())
    return -1;
  if (maxout < 0)
    errh->warning("no routes");
  if (maxout >= noutputs())
      return errh->error("need %d or more output ports", maxout + 1);

  return 0;
}

int
LookupIPRouteMP::initialize(ErrorHandler *)
{
#if HAVE_USER_MULTITHREAD
  _cache = (struct cache_entry*)malloc(sizeof(*_cache) * click_max_cpu_ids());
#endif

  click_chatter("LookupIPRouteMP alignment: %p, %p",
                &(_cache[0]._last_addr_1), &(_cache[1]._last_addr_1));
  for (unsigned i=0; i<click_max_cpu_ids(); i++) {
    _cache[i]._last_addr_1 = IPAddress();
    _cache[i]._last_addr_2 = IPAddress();
  }
  return 0;
}


#if HAVE_BATCH
void
LookupIPRouteMP::push_batch(int, PacketBatch *batch)
{
	int max = _t.size();
	PacketBatch* out[max + 1]; //Array for each entry, last is for unrouted packets
	bzero(out,sizeof(PacketBatch*) * (max+1));
	IPAddress*  out_gw = new IPAddress[max + 1]; //Gw for each entry, last is used as a temp
	bzero(out_gw,sizeof(IPAddress) * (max+1));


	IPAddress last_addr = 0;
	int last_entry = 0;
	IPAddress last_addr2 = 0;
	int last_entry2 = 0;

	auto fnt = [this,&out_gw,&max,&last_addr,&last_entry,&last_addr2,&last_entry2](Packet* p){
		IPAddress a = p->dst_ip_anno();
		int o = max;

		if (a && a == last_addr) {
			o = last_entry;
		} else if (a && a == last_addr2) {
			o = last_entry2;
		} else if (_t.lookup(a, out_gw[max], o)) {
			last_addr2 = last_addr;
			last_entry2 = last_entry;
			last_addr = a;
			last_entry = o;
			out_gw[o] = out_gw[max];
		} else {
			static int complained = 0;
			if (++complained <= 5)
				click_chatter("LookupIPRouteMP: no route for %s", a.unparse().c_str());
			goto no_route;
		}


		if (out_gw[o])
			p->set_dst_ip_anno(out_gw[o]);

		no_route:
		if (o == -1) o = max;
		return o;
	};
	CLASSIFY_EACH_PACKET((max + 1),fnt,batch,checked_output_push_batch);
	delete[] out_gw;
}
#endif //HAVE_BATCH
void
LookupIPRouteMP::cleanup(CleanupStage)
{
#if HAVE_USER_MULTITHREAD
    free(_cache);
#endif
}

void
LookupIPRouteMP::push(int, Packet *p)
{
  IPAddress a = p->dst_ip_anno();
  IPAddress gw;
  int ifi = -1;
  struct cache_entry *e = &_cache[click_current_cpu_id()];

  if (a) {
    if (a == e->_last_addr_1) {
      if (e->_last_gw_1)
	p->set_dst_ip_anno(e->_last_gw_1);
      output(e->_last_output_1).push(p);
      return;
    }
    else if (a == e->_last_addr_2) {
      if (e->_last_gw_2)
	p->set_dst_ip_anno(e->_last_gw_2);
      output(e->_last_output_2).push(p);
      return;
    }
  }

  if (_t.lookup(a, gw, ifi)) {
    e->_last_addr_2 = e->_last_addr_1;
    e->_last_gw_2 = e->_last_gw_1;
    e->_last_output_2 = e->_last_output_1;
    e->_last_addr_1 = a;
    e->_last_gw_1 = gw;
    e->_last_output_1 = ifi;
    if (gw)
      p->set_dst_ip_anno(gw);
    output(ifi).push(p);
  } else {
    click_chatter("LookupIPRouteMP: no route for %x", a.addr());
    p->kill();
  }
}

CLICK_ENDDECLS
EXPORT_ELEMENT(LookupIPRouteMP)
