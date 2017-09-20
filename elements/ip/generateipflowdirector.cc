// -*- c-basic-offset: 4; related-file-name: "generateipflowdirector.hh" -*-
/*
 * GenerateIPFlowDirector.{cc,hh} -- element generates Flow Director patterns out of input traffic
 * Georgios Katsikas, Tom Barbette
 *
 * Copyright (c) 2017 Georgios Katsikas, RISE SICS AB
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

int GenerateIPFlowDirector::_nb_cores = 16;

/**
 * Flow Director rules' generator out of incoming traffic.
 */
GenerateIPFlowDirector::GenerateIPFlowDirector() : GenerateIPFilter()
{
}

GenerateIPFlowDirector::~GenerateIPFlowDirector()
{
}

int
GenerateIPFlowDirector::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int nb_cores;

    if (Args(conf, this, errh)
            .read_p("NB_CORES", nb_cores)
            .consume() < 0)
        return -1;

    if (nb_cores <= 0) {
        errh->error("NB_CORES must be positive");
        return -1;
    } else {
        _nb_cores = nb_cores;
    }

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
    StringAccum acc;

    int n = 0;
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
            return "Impossible to lower the rules and keep the choosen fields";
        }
    }

    int i = 0;
    for (auto flow : g->_map) {
        acc << "pattern ip src " << flow.flowid().saddr() << '/' << String(32 - n) << " and "
            << " ip dst "        << flow.flowid().daddr() << '/' << String(32 - n);

        // TODO: Read ip proto and discriminate between TCP and UDP
        // if (g->_keep_sport)
        //     acc << " and tcp port src " << flow.flowid().sport();
        // if (g->_keep_dport) {
        //     acc << " and tcp port dst " << flow.flowid().dport();
        // }

        // Send flows to cores in a circular fashion
        acc << " action queue index " << i % _nb_cores << "\n";

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
