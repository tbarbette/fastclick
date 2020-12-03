/*
 * crossrss.{cc,hh} -- TCP & UDP load-balancer
 * Tom Barbette
 *
 * Copyright (c) 2019-2020 KTH Royal Institute of Technology
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
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <click/flow/flow.hh>

#include "crossrss.hh"

#include <random>

CLICK_DECLS

CrossRSS::CrossRSS()
{
}

CrossRSS::~CrossRSS()
{
}


    inline int CrossRSS::get_core_for_hash(int hash, int id, int cores_per_server) {
//        return cantor(hash, id) % cores_per_server;
            return _machines[id].map[hash];
    }
int
CrossRSS::configure(Vector<String> &conf, ErrorHandler *errh)
{

    if (Args(this, errh).bind(conf)
            .read_or_set("TABLE_SIZE", _table_size, 128)
            .read_or_set("CORES_PER_SERVER", _cores_per_server, 4)
               .consume() < 0)
		return -1;

    _dsts.resize(noutputs());

    if (parseLb(conf, this, errh) < 0)
            return -1;

    if (Args(this, errh).bind(conf).complete() < 0)

    click_chatter("%p{element} has %d routes",this,_dsts.size());

    _machines.resize(noutputs());
    for (int i =0; i < _machines.size(); i ++) {
        _machines[i].cores.resize(_cores_per_server);
        _machines[i].map.resize(_table_size);
        std::mt19937 engine(i);
        std::uniform_int_distribution<> dist(0,_cores_per_server -1);
        for (int j = 0; j < _table_size; j++) {
            _machines[i].map[j] = dist(engine);
        }
    }

    auto& wh = _weights_helper.write_begin();
    wh.resize(_table_size);
    for (int i = 0 ; i < wh.size(); i++) {
        wh[i] = _selector[i % _selector.size()];
        click_chatter("[%d] -> %d/%d (%d,%d,%d,%d)",i, wh[i]
                ,get_core_for_hash(i,wh[i],_cores_per_server)
                ,get_core_for_hash(i,0,_cores_per_server)
                ,get_core_for_hash(i,1,_cores_per_server)
                ,get_core_for_hash(i,2,_cores_per_server)
                ,get_core_for_hash(i,3,_cores_per_server)
                );
    }
    _weights_helper.write_commit();
    return 0;
}

int CrossRSS::initialize(ErrorHandler *errh)
{
    return 0;
}


bool CrossRSS::new_flow(CrossRSSEntry* flowdata, Packet* p)
{
    int server = pick_server(p);
/*
    auto & wh = _weights_helper.read_begin();
    int hash = hash_4tuple(p, wh.size());
    auto r =  wh[hash];

    _weights_helper.read_end();*/
//    click_chatter("Hash %d (sz %d) -> %d [core %d]",hash, wh.size(),r,hash_4tuple(p,4));
//    assert(server == r);

    flowdata->chosen_server = server;

    return true;
}

void CrossRSS::push_flow(int, CrossRSSEntry* flowdata, PacketBatch* batch)
{

    FOR_EACH_PACKET(batch, p) {
        int hash = LoadBalancer::hash_4tuple(p, _table_size);

        SET_PAINT_ANNO(p,get_core_for_hash(hash, flowdata->chosen_server, _cores_per_server));
    }
    output_push_batch(flowdata->chosen_server, batch);
}

int
CrossRSS::handler(int op, String& s, Element* e, const Handler* h, ErrorHandler* errh) {
    CrossRSS *cs = static_cast<CrossRSS *>(e);
    return cs->lb_handler(op, s, h->read_user_data(), h->write_user_data(), errh);
}

String
CrossRSS::read_handler(Element *e, void *thunk) {
    CrossRSS *cs = static_cast<CrossRSS *>(e);
    return cs->lb_read_handler(thunk);
}

int
CrossRSS::write_handler(const String &in_str, Element *e, void *thunk, ErrorHandler *errh)
{
    CrossRSS *fs = (CrossRSS *)e;
    String str = cp_uncomment(in_str);

    switch ((intptr_t)thunk) {
        case h_cpu_load:
        {
            int machine = 0;
            int core = 0;
            int load = 0;
            if (!IntArg().parse(cp_shift_spacevec(str), machine))
                return errh->error("'load' first word should be unsigned (machine)");
            if (!IntArg().parse(cp_shift_spacevec(str), core))
                return errh->error("'load' second word should be unsigned (core)");
            if (!IntArg().parse(cp_shift_spacevec(str), load))
                return errh->error("'load' third word should be unsigned (load)");
            if (machine >= fs->_machines.size()) {
                click_chatter("ERROR : Unkown machine id %d", machine);
                return -1;
            }
            fs->_machines[machine].cores[core].load = fs->_alpha * (load - fs->_machines[machine].cores[core].raw) + (1- fs->_alpha) * fs->_machines[machine].cores[core].load;
            fs->_machines[machine].cores[core].raw = load;
        return 0;
        }
        case h_cross_rehash:
        {
            auto &w = fs->_weights_helper.write_begin();
            for (int i = 0; i < w.size(); i++) {
                int l = -1;
                int min_l = INT_MAX;
                for (int j = 0; j < fs->_machines.size(); j++) {
                    int core = fs->get_core_for_hash(i,j,fs->_cores_per_server);
                    if (fs->_machines[j].cores[core].load < min_l) {
                        l = j;
                        min_l = fs->_machines[j].cores[core].load;
                    }
                }
                w[i] = l;
                //click_chatter("Weight[%d] %d (%d)", i, l, min_l);
            }
            fs->_weights_helper.write_commit();
        }
        return 0;
    default:

        return fs->lb_write_handler(in_str,thunk,errh);
    }
}

void
CrossRSS::add_handlers()
{
    add_lb_handlers<CrossRSS>(this);
    add_write_handler("cpu_load", write_handler, h_cpu_load);
    add_write_handler("rehash", write_handler, h_cross_rehash);
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(CrossRSS)
ELEMENT_MT_SAFE(CrossRSS)
