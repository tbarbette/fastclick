/*
 * ctxiproute.{cc,hh} -- element looks up next-hop address in static
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
#include <click/ipaddress.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>

#include "ctxiproute.hh"

CLICK_DECLS

CTXIPRoute::CTXIPRoute()
{
}

CTXIPRoute::~CTXIPRoute()
{
}

int
CTXIPRoute::configure(Vector<String> &conf, ErrorHandler *errh)
{
    //See CTXDispatcher for logic
    VirtualFlowManager::fcb_builded_init_future()->post(new Router::FctChildFuture([this,conf](ErrorHandler *errh){
       int maxout = -1;


        rules.resize(conf.size() + 1);
        _gw.resize(noutputs());
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

        if (output_num >= noutputs()) {
            return errh->error("%p{element}: Port index %d is out of bound. Use -1 or drop to drop flows from a given IP or Range.", output_num);
        }
        if (ok && output_num >= 0) {
            rules[i] = FlowClassificationTable::make_ip_mask(dst, mask);
            rules[i].output = output_num;
            rules[i].is_default = false;
            if (_gw[output_num] && _gw[output_num] != gw) {
                return errh->error("Gateway must be identical for each output");
                //This is not a strict limitation, only to reuse CTXDispatcher get_table code
            }
            _gw[output_num] = gw;
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
        rules[conf.size()]=FlowClassificationTable::make_drop_rule();
        return 0;
    }, this, CTXManager::ctx_builded_init_future()));

  return 0;
}

void CTXIPRoute::push_flow(int, int* flowdata, PacketBatch* batch) {
    if (*flowdata >= 0 && *flowdata < noutputs()) { //If valid output
        if (_gw[*flowdata] != IPAddress(0)) {
            FOR_EACH_PACKET(batch,p)
                p->set_dst_ip_anno(_gw[*flowdata]);
        } else {
            output_push_batch(*flowdata, batch);
        }
    } else //Invalid output
        batch->fast_kill();

}

CLICK_ENDDECLS
EXPORT_ELEMENT(CTXIPRoute)
