// -*- c-basic-offset: 4; related-file-name: "generateipflowdirector.hh" -*-
/*
 * GenerateIPFlowDirector.{cc,hh} -- element generates Flow Director patterns
 * out of input traffic, following DPDK's flow API syntax.
 * Georgios Katsikas, Tom Barbette
 *
 * Copyright (c) 2017 Georgios Katsikas, RISE SICS
 * Copyright (c) 2017 Tom Barbette, University of Liège
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
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>

#include "generateipflowdirector.hh"

CLICK_DECLS

static const uint16_t DEF_NB_CORES = 16;

/**
 * Flow Director rules' generator out of incoming traffic.
 */
GenerateIPFlowDirector::GenerateIPFlowDirector() :
        _port(0), _nb_cores(DEF_NB_CORES), GenerateIPFilter()
{
}

GenerateIPFlowDirector::~GenerateIPFlowDirector()
{
}

int
GenerateIPFlowDirector::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_mp("PORT", _port)
            .read_p("NB_CORES", _nb_cores)
            .consume() < 0)
        return -1;

    if (_nb_cores == 0) {
        errh->error("NB_CORES must be a positive integer");
        return -1;
    }

    // Default mask
    _mask = IPFlowID(0xffffffff, (_keep_sport?0xffff:0), 0xffffffff, (_keep_dport?0xffff:0));

    return GenerateIPFilter::configure(conf, errh);
}

int
GenerateIPFlowDirector::initialize(ErrorHandler *errh)
{
    return GenerateIPFilter::initialize(errh);
}

void
GenerateIPFlowDirector::cleanup(CleanupStage)
{
}

Packet *
GenerateIPFlowDirector::simple_action(Packet *p)
{
    return GenerateIPFilter::simple_action(p);
}

#if HAVE_BATCH
PacketBatch*
GenerateIPFlowDirector::simple_action_batch(PacketBatch *batch)
{
    return GenerateIPFilter::simple_action_batch(batch);
}
#endif

String
GenerateIPFlowDirector::read_handler(Element *e, void *user_data)
{
    GenerateIPFlowDirector *g = static_cast<GenerateIPFlowDirector *>(e);
    if (!g) {
        return "GenerateIPFlowDirector element not found";
    }

    StringAccum acc;

    uint8_t n = 0;
    while (g->_map.size() > g->_nrules) {
        HashTable<IPFlow> newmap;
        ++n;
        g->_mask = IPFlowID(
            IPAddress::make_prefix(32 - n), g->_mask.sport(),
            IPAddress::make_prefix(32 - n), g->_mask.dport()
        );
        for (auto flow : g->_map) {
            flow.setMask(g->_mask);
            newmap.find_insert(flow);
        }
        g->_map = newmap;
        if (n == 32) {
            return "Impossible to lower the rules and keep the chosen fields";
        }
    }

    uint64_t i = 0;
    for (auto flow : g->_map) {
        acc << "flow create "<< String(g->_port) <<
               " ingress pattern ipv4" <<
               " src spec " << flow.flowid().saddr() <<
               " src mask " << IPAddress::make_prefix(32 - n) <<
               " dst spec " << flow.flowid().daddr() <<
               " dst mask " << IPAddress::make_prefix(32 - n) <<
               " /";

        /**
         * TODO: Read IP proto and discriminate between TCP and UDP
         */
        /*
        if (g->_keep_sport)
            acc << " tcp src is " << flow.flowid().sport();
        if (g->_keep_dport)
            acc << " tcp dst is " << flow.flowid().dport();
        if ((g->_keep_sport) || (g->_keep_dport))
            acc << " / ";
        */

        // Round-robin across the available CPU cores
        uint16_t core = (i % g->_nb_cores);
        assert((core >= 0) && (core < g->_nb_cores));
        acc << " end actions queue index " << core << " / end\n";

        i++;
    }

    return acc.take_string();
}

void
GenerateIPFlowDirector::add_handlers()
{
    add_read_handler("dump", read_handler);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk GenerateIPFilter)
EXPORT_ELEMENT(GenerateIPFlowDirector)
