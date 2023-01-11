// -*- c-basic-offset: 4; related-file-name: "devicebalancer.hh" -*-
/*
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
#include <click/args.hh>
#include <click/error.hh>
#include <click/multithread.hh>
#include <click/straccum.hh>

#if HAVE_NUMA
#include <click/numa.hh>
#endif
#include <rte_flow.h>
#include "devicebalancer.hh"
#ifdef HAVE_BPF
#include "xdploader.hh"
#endif
#include "../flow/flowipmanagerbucket.hh"

#include <string>

#if HAVE_FLOW_API
    #include <click/flowrulemanager.hh>
#endif

CLICK_DECLS

DeviceBalancer::DeviceBalancer() : _timer(this) {
}

DeviceBalancer::~DeviceBalancer() {
}

/**
 *
 */
int
DeviceBalancer::configure(Vector<String> &conf, ErrorHandler *errh) {
    Element* dev = 0;
    String method;
    String target;
    String source;
    String cycles;
    int startcpu;
    int max_cpus;
    bool havemax;
    Element* manager = 0;
    if (Args(this, errh).bind(conf)
        .read_mp("METHOD", method)
        .read_mp("DEV", dev)
        .read_or_set("CORE_OFFSET", _core_offset, 0)
        .read_or_set("TIMER", _tick, 100)
        .read("TIMER_MAX", _tick_max).read_status(havemax)
        .read_or_set("CPUS", max_cpus, click_max_cpu_ids())
        .read_or_set("TARGET", target, "load")
        .read_or_set("STARTCPU", startcpu, -1)
        .read_or_set("UNDERLOAD", _underloaded_thresh, 0.25)
        .read_or_set("OVERLOAD", _overloaded_thresh, 0.75)
        .read_or_set("CYCLES", cycles, "cycles")
        .read_or_set("AUTOSCALE", _autoscale, false)
        .read_or_set("ACTIVE", _active, true)
        .read_or_set("VERBOSE", _verbose, true)
        .read("MANAGER",ElementArg(), manager)
        .consume() < 0)
        return -1;

    _max_cpus = max_cpus;

    if (!havemax)
        _tick_max = _tick;

    if (cycles == "cycles") {
        _load = LOAD_CYCLES;
    } else  if (cycles == "cyclesqueue") {
        _load = LOAD_CYCLES_THEN_QUEUE;
    } else  if (cycles == "cpu") {
        _load = LOAD_CPU;
    } else  if (cycles == "queue") {
        _load = LOAD_QUEUE;
    } else  if (cycles == "realcpu") {
        _cpustats.resize(max_cpus);
        _load = LOAD_REALCPU;
    } else {
        return errh->error("Unknown cycle method !");
    }

    if (startcpu == -1) {
        startcpu = max_cpus;
    }

    _startwith = startcpu;

    EthernetDevice* fd = (EthernetDevice*)dev->cast("EthernetDevice");
    if (!fd) {
        return errh->error("Not an Ethernet Device");
    } else {
        click_chatter("%p{element} will balance %p{element}", this, dev);
    }

    if (set_method(std::string(method.c_str()), fd) != 0) {
        return errh->error("Unknown method %s", method.c_str());
    }

    click_chatter("Balancing method is %s", get_method()->name().c_str());

    // Detect if the method is based on a device (very likely) and if it is a DPDK method or not
    if (dynamic_cast<BalanceMethodDevice*>(get_method()) != 0) {
        BalanceMethodDevice* method = dynamic_cast<BalanceMethodDevice*>(get_method());
        method->_is_dpdk = dev->cast("DPDKDevice");
        if (method->_is_dpdk) {
            click_chatter("DPDK mode");
        } else {
            click_chatter("Kernel mode");
        }
    }


    //Configure specific parameters for every methods

    //RSS
    if (dynamic_cast<MethodRSS*>(get_method()) != 0) {
        //Element* e = 0;
        MethodRSS* rss = dynamic_cast<MethodRSS*>(get_method());
        if (Args(this, errh).bind(conf)
                //.read("VERIFIER", e)
                .read_or_set("RETA_SIZE", rss->_reta_size, 128)
                .read_or_set("MARK", rss->_use_mark, true)
                .read_or_set("GROUP", rss->_use_group, true)
                .consume() < 0)
            return -1;
        /*if (e) {
            _verifier = (RSSVerifier*)e->cast("RSSVerifier");
            if (!_verifier)
                return errh->error("Verifier must be of the type RSSVerifier");
        }*/
    }

    //RSS++
    if (get_method()->name() == "rsspp") {
        Element* e = 0;
        double t;
        double i;
        double threshold;
        MethodRSSPP* rsspp = dynamic_cast<MethodRSSPP*>(get_method());
        if (Args(this, errh).bind(conf)
                .read_or_set("TARGET_LOAD", t, 0.8)
                .read("RSSCOUNTER", e)
                .read_or_set("IMBALANCE_ALPHA", i, 1)
                .read_or_set("IMBALANCE_THRESHOLD", threshold, 0.02) //Do not scale core underloaded or overloaded by this threshold
                .read_or_set("DANCER", rsspp->_dancer, false)
                .read_or_set("NUMA", rsspp->_numa, false)
                .consume() < 0)
            return -1;
        rsspp->_target_load = t;
        rsspp->_imbalance_alpha = i;
        rsspp->_threshold = threshold;
    #if HAVE_NUMA
        if (rsspp->_numa)
            rsspp->_numa_num = Numa::get_max_numas();
        else
    #endif
            rsspp->_numa_num = 1;

        //Reta_size must be set before this
        if (e) {
            rsspp->_counter = (AggregateCounterVector*)e->cast("AggregateCounterVector");
            if (!rsspp->_counter) {
    #ifdef HAVE_BPF
                rsspp->_counter = (XDPLoader*)e->cast("XDPLoader");
    #endif
                if (!rsspp->_counter) {
                    return errh->error("COUNTER must be of the type AggregateCounterVector or XDPLoader");
                }
                rsspp->_counter_is_xdp = true;
            } else {
                rsspp->_counter_is_xdp = false;
            }
        } else {
            return errh->error("You must set a RSSCOUNTER element");
        }
    }

    //Verify and set a potential FlowIPManager argument
    if (manager) {
        auto listener = dynamic_cast<MigrationListener*>(manager);

        if (!listener)
            return errh->error("The flow manager is not of type MigrationListener");

        set_migration_listener(listener);
    }

    //Set the load target
    if (target=="load")
        _target = TARGET_LOAD;
    else if (target == "balance")
        _target = TARGET_BALANCE;
    else
        return errh->error("Unknown target %s", target.c_str());

    if (Args(this, errh).bind(conf)
            .complete() < 0)
        return -1;


    return 0;
}


int
DeviceBalancer::initialize(ErrorHandler *errh) {
    int startwith = _startwith;
    if (_method->initialize(errh, startwith) != 0)
        return -1;

    if (_manager) {
        MethodRSS* method = dynamic_cast<MethodRSS*>(_method);
        if (method != 0 && dynamic_cast<FlowIPManagerBucket*>(_manager) != 0) {
            dynamic_cast<FlowIPManagerBucket*>(_manager)->post([this,method](){
                    _manager->init_assignment(method->_table.data(), method->_table.size());
                    return 0;
            });
        }
    }
    for (int i = 0; i < startwith; i++) {
       _used_cpus.push_back(CpuInfo{.id= i,.last_cycles=0});
    }
    for (int i = _max_cpus  - 1; i >=startwith; i--) {
       _available_cpus.push_back(i);
    }
    _timer.initialize(this);
    _current_tick = _tick;
        _stats.resize(10);
    if (_active)
        _timer.schedule_after_msec(_current_tick);
    return 0;
}


void
DeviceBalancer::run_timer(Timer* t) {
    std::vector<std::pair<int,float>> load;
    float totload = 0;
    if (_load == LOAD_CPU) {
        for (unsigned u = 0; u < (unsigned)_used_cpus.size(); u++) {
            int i = _used_cpus[u].id;
            float l = master()->thread(i)->load();
            load.push_back(std::pair<int,float>{i,l});
            totload += l;
        }
    } else if (_load == LOAD_CYCLES || _load == LOAD_CYCLES_THEN_QUEUE) {
        /**
         * Use the amount of cycles since last tick, more precise than LOAD_CPU.
         * We use the raw amount of cycles, divided by the total amount of cycles for all CPUs
         * This will give a number between 0 and 1, 1 being the total for all CPUs
         * We therefore multiply the load by the total (unprecise) load, to give a realistic
         * scale in term of "amount of cores" load but giving a relative better precision.
         */
        unsigned long long utotload = 0;
        Vector<unsigned long long> uload;
        for (unsigned u = 0; u < (unsigned)_used_cpus.size(); u++) {
            int phys_id = _used_cpus[u].id;
            unsigned long long ul = master()->thread(phys_id)->useful_kcycles();
            unsigned long long pl = _used_cpus[u].last_cycles;
            unsigned long long dl = ul - pl;
            _used_cpus[u].last_cycles = ul;
            uload.push_back(dl);
            utotload += dl;
            //click_chatter("core %d kcycles %llu %llu load %f", phys_id, ul, dl, master()->thread(phys_id)->load());
            totload += master()->thread(phys_id)->load();
        }
        if (utotload > 0) {
                for (int u = 0; u < _used_cpus.size(); u++) {
                    double pc = (double)uload[u] / (double)utotload;
                    if (totload * pc > 1.0)
                        totload = 1.0 / pc;
                }
        }

        int overloaded = 0;

        for (unsigned u = 0; u < (unsigned)_used_cpus.size(); u++) {
            int i = _used_cpus[u].id;
            float l;
            if (utotload == 0)
                l = 0;
            else {
                double pc = (double)uload[u] / (double)utotload;
                l = pc * totload;

                //click_chatter("core %d load %f -> %f", i, pc, l);
            }
            assert(l <= 1 && l>=0);
            if (l > 0.98)
                overloaded ++;
            load.push_back(std::pair<int,float>{i,l});
        }

        if (_load == LOAD_CYCLES_THEN_QUEUE && overloaded > 1) {
            DPDKDevice* fd = (DPDKDevice*)((BalanceMethodDevice*)_method)->_fd;
            int port_id = fd->port_id;
            float rxdesc = fd->get_nb_rxdesc();
            for (int u = 0; u < _used_cpus.size(); u++) {
                int i = _used_cpus[u].id;
                int v = rte_eth_rx_queue_count(port_id, i);
                if (v < 0) {
            click_chatter("WARNING : unsupported rte_eth_rx_queue_count for queue %d, error %d", i, v);
            continue;
                }
                float l = (float)v / rxdesc;
                //click_chatter("Core %d %f %f",u,load[u].second,l);

                load[u].second = load[u].second * 0.90 + 0.10 * l;
            }
        }
    } else if (_load == LOAD_REALCPU) {
        Vector<float> l(num_max_cpus(), 0);
        unsigned long long totalUser, totalUserLow, totalSys, totalIdle, totalIoWait, totalIrq, totalSoftIrq;
        int cpuId;
        FILE* file = fopen("/proc/stat", "r");
        char buffer[1024];
        char *res = fgets(buffer, 1024, file);
        assert(res);
        while (fscanf(file, "cpu%d %llu %llu %llu %llu %llu %llu %llu", &cpuId, &totalUser, &totalUserLow, &totalSys, &totalIdle, &totalIoWait, &totalIrq, &totalSoftIrq) > 0) {
        if (cpuId < l.size()) {
                unsigned long long newTotal = totalUser + totalUserLow + totalSys + totalIrq + totalSoftIrq;
                unsigned long long tdiff =  (newTotal - _cpustats[cpuId].lastTotal);
                unsigned long long idiff =  (totalIdle - _cpustats[cpuId].lastIdle);
                if (tdiff + idiff > 0)
                    l[cpuId] =  (float)tdiff / (float)(tdiff + idiff);
                _cpustats[cpuId].lastTotal = newTotal;
                _cpustats[cpuId].lastIdle = totalIdle;
                //click_chatter("C %d total %d %d %d",cpuId,newTotal,tdiff, idiff);
                res = fgets(buffer, 1024, file);
            }
        }
        fclose(file);
        for (int u = 0; u < _used_cpus.size(); u++) {
            click_chatter("Used %d load %f",u,l[u]);
            int i = _used_cpus[u].id;
            float cl = l[i];
            load.push_back(std::pair<int,float>{i,cl});
            totload += cl;
        }
    } else { //_load == LOAD_QUEUE
        DPDKDevice* fd = (DPDKDevice*)((BalanceMethodDevice*)_method)->_fd;
        int port_id = fd->port_id;
        float rxdesc = fd->get_nb_rxdesc();
        for (unsigned u = 0; u < _used_cpus.size(); u++) {
            int i = _used_cpus[u].id;
            int v = rte_eth_rx_queue_count(port_id, i);
            float l = (float)v / rxdesc;
            load.push_back(std::pair<int,float>{i,l});
            totload += l;
        }
    }

    if (unlikely(_verbose > 1)) {
        String s = "load ";
        for (unsigned u = 0; u < load.size(); u++) {
            s += String(load[u].second) + " ";
        }
        s += "\n";
        click_chatter("%s",s.c_str());
    }

    if (unlikely(_target == TARGET_BALANCE)) {
        float target = totload / num_max_cpus();
        for (unsigned i = 0; i < (unsigned)load.size(); i ++) {
            if (target < 0.1)
                load[i].second = 0.5;
            else {
            // 0  0.2  -> 0
            // 0.1 0.2 -> 0.25
            // 0.2 0.2 -> 0.5
            // 0.3 0.2 -> 0.75
            // 0.4 0.2 -> 1
//                click_chatter("Load %f target %f -> ",load[i].second, target);
                load[i].second =  load[i].second / target / 2;
            }

            if (load[i].second > 1)
                load[i].second = 1;
            if (load[i].second < 0)
                load[i].second = 0;
            //click_chatter("%f",load[i].second);
        }

    }

 /*   assert(_method);
    assert(load.size() > 0);
    for (int i = 0; i < load.size(); i++) {
    click_chatter("Load of core %d is %f",load[i].first,load[i].second);
    }*/

    _method->rebalance(load);
    if (_active)
        _timer.schedule_after_msec(_current_tick);
} //end of balancing timer

DeviceBalancer::CpuInfo
DeviceBalancer::make_info(int id) {
    CpuInfo i;
    i.id = id;
    i.last_cycles = master()->thread(id)->useful_kcycles();
    return i;
}

enum {h_active, h_autoscale, h_force_cpu, h_run_stats, h_run_stats_imbalance_first, h_run_stats_time_first};


String
DeviceBalancer::read_param(Element *e, void *thunk)
{
    DeviceBalancer *td = (DeviceBalancer *)e;
    switch((uintptr_t) thunk) {
    case h_active:
        return String(td->_active);
    case h_autoscale:
        return String(td->_autoscale);
    case h_run_stats: {
        StringAccum acc;
        for (int j = 0; j < 10; j++) {
            int count = 0;
            double imbalance = 0;
            uint64_t time = 0;
                std::vector<DeviceBalancer::RunStat> &v = td->_stats;
                count += v[j].count;
                imbalance += v[j].imbalance;
                time += v[j].time;
            if (count > 0)
                acc << j << " " << count << " " << imbalance/count << " " << time/count << "\n";
            else
                acc << j << " 0 nan nan\n";
        }
        return acc.take_string();
       }
   case h_run_stats_imbalance_first: {
            int count = 0;
            double imbalance = 0;
            std::vector<DeviceBalancer::RunStat> &v = td->_stats;
                count += v[0].count;
                imbalance += v[0].imbalance;
            return String(imbalance/count);
      }
   case h_run_stats_time_first: {
            int count = 0;
            uint64_t time = 0;
            std::vector<DeviceBalancer::RunStat> &v = td->_stats;
                count += v[0].count;
                time += v[0].time;
            if (count > 0) {
                return String(time/count);
            } else
                return "nan";
      }
    default:
        return String();
    }
}

int
DeviceBalancer::write_param(const String &in_s, Element *e, void *vparam,
                     ErrorHandler *errh)
{
    DeviceBalancer *db = (DeviceBalancer *)e;
    String s = cp_uncomment(in_s);
    switch ((intptr_t)vparam) {
    case h_active: {
        bool active;
        if (!BoolArg().parse(s, active))
            return errh->error("type mismatch");
        if (active && !db->_active)
        db->_timer.schedule_after_msec(db->_current_tick);

        db->_active = active;
        break;
    }
    case h_autoscale: {
        bool active;
        if (!BoolArg().parse(s, active))
            return errh->error("type mismatch");
        db->_autoscale = active;
        break;
    }
    case h_force_cpu: {
        int cpus;
        if (!IntArg().parse(s, cpus))
            return errh->error("type mismatch");
        bool moved = false;
        if (cpus > (int)db->_used_cpus.size()) {
            for (unsigned i = 0; i < cpus-db->_used_cpus.size(); i++) {
                if (db->_available_cpus.size() > 0) {
                    db->addCore();
                    moved = true;
                }
            }
        }
        else if (cpus < (int)db->_used_cpus.size()) {
            //in_s = "UNSUPPORTED";
            return -1;
        //for (int i = 0; i < db->_used_cpus.size() - cpus; i++) {

            /*if (db->_used_cpus.size() > 0) {
                db->removeCore();
                moved = false;
            }*/
        //}
        }
        if (moved) {
            db->_method->cpu_changed();
        }
        //in_s = cpus-db->_used_cpus.size();
        break;
    }
    }
    return 0;
}

void
DeviceBalancer::add_handlers()
{
    add_read_handler("active", read_param, h_active, Handler::CHECKBOX);
    add_read_handler("autoscale", read_param, h_autoscale, Handler::CHECKBOX);
    add_write_handler("active", write_param, h_active);
    add_write_handler("autoscale", write_param, h_autoscale);
    add_write_handler("cpus", write_param, h_force_cpu);
    add_read_handler("run_stats", read_param, h_run_stats);
    add_read_handler("run_stats_imbalance_first", read_param, h_run_stats_imbalance_first);
    add_read_handler("run_stats_time_first", read_param, h_run_stats_time_first);
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(load userlevel rsspp flow dpdk)
EXPORT_ELEMENT(DeviceBalancer)
