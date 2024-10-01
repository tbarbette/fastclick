/**
 * RSS++
 */

#include "solver.hh"

MethodRSSPP::MethodRSSPP(NICScheduler* b, EthernetDevice* fd) : MethodRSS(b,fd) {
    _use_mark = true;
}


int MethodRSSPP::initialize(ErrorHandler* errh, int startwith) {
    if (MethodRSS::initialize(errh, startwith) != 0)
        return -1;
    load_tracker_initialize(errh);

#ifdef HAVE_BPF
    if (_counter_is_xdp) {
        click_chatter("Resizing count to %d",_reta_size);
        _count.resize(_reta_size);
        _xdp_table_fd = ((XDPLoader*)_counter)->get_map_fd("count_map");
        if (!_xdp_table_fd)
            return errh->error("Could not find map !");
    }
#endif
    return 0;
}
#define EPSILON 0.0001f



struct MachineLoad {
    MachineLoad() : N(0), total_load(0), target(0) {

    }
    int N;
    float total_load;
    float target;
};

struct SocketLoad : MachineLoad {
    SocketLoad() : imbalance(),uid(),oid() {
    }

    std::vector<float> imbalance;
    std::vector<int> uid;
    std::vector<int> oid;
};

#define print_cpu_assign() {for (int i = 0; i < rload.size(); i++) {click_chatter("CPU %d -> %d/%d",i, rload[i].first,load[i].cpu_phys_id);}}
#define get_node_count(i) ((_counter_is_xdp)?_count[i].count:((AggregateCounterVector*)_counter)->find_node_nocheck(i).count)
#define get_node_variance(i) ((_counter_is_xdp)?_count[i].variance:((AggregateCounterVector*)_counter)->find_node_nocheck(i).variance)
#define get_node_moved(i) (_count[i).moved]


inline void MethodRSSPP::apply_moves(std::function<int(int)> cpumap, std::vector<std::vector<std::pair<int,int>>> omoves, const Timestamp &begin) {
        if (balancer->_manager) {
            for (int i = 0; i < omoves.size(); i++) {
                if (omoves[i].size() > 0) {
                    int from_phys_id = cpumap(i);
                    balancer->_manager->pre_migrate((EthernetDevice*)_fd,from_phys_id,omoves[i]);
                }
            }
        }

        Timestamp t = Timestamp::now_steady();
        int64_t v = (t-begin).usecval();
        if (unlikely(balancer->verbose() || v > 100))
            click_chatter("Solution computed in %ld usec, %lu moves", v, omoves.size());

        update_reta();
        if (balancer->_manager) {
            for (int i = 0; i < omoves.size(); i++) {
                if (omoves[i].size() > 0) {
                    int from_phys_id =  cpumap(i);
                    balancer->_manager->post_migrate((EthernetDevice*)_fd,from_phys_id);
                }
            }
        }
}


void MethodRSSPP::rebalance(std::vector<std::pair<int,float>> rload) {
    click_jiffies_t now = click_jiffies();
#ifdef HAVE_BPF
    if (_counter_is_xdp) {
        click_chatter("Reading XDP table");
        int cpus = balancer->num_max_cpus();
        unsigned int nr_cpus = bpf_num_possible_cpus();
        uint64_t values[nr_cpus];
        for (uint32_t key = 0; key < _count.size(); key++) {
            if (bpf_map_lookup_elem(_xdp_table_fd, &key, values)) {
                click_chatter("XDP lookup failed");
                continue;
            }
            uint64_t tot = 0;
//            for (int i = 0; i < nr_cpus; i++) {
                tot += values[_table[key]];
//            if (values[i] && i != _table[key])
            //if (values[i] != 0)
//                click_chatter("BPF map ha value on core %d, but RSS is assigned to %d. Val %d",i,_table[key],values[i]);
  //              tot += values[i];
//            }
//            tot -= _count[key].count;

            _count[key].count  = tot;
            uint64_t var =  (_count[key].variance  / 3) + (2 * tot / 3);
            _count[key].variance  = var;
            if (unlikely(tot != 0 && balancer->verbose()))
                click_chatter("Key %d core %d val %d, var %f",key,_table[key], tot,(float)min(tot,var) / (float)max(tot,var));
        }
    }
#endif

    Timestamp begin = Timestamp::now_steady();
    float _min_load = 0.01;
    float _threshold_force_overload = 0.90;
    float _load_alpha = 1;
    //float _high_load_threshold = 0.;
    const float _imbalance_threshold = _threshold / 2;
    assert(_imbalance_threshold <= (_threshold / 2) + EPSILON); //Or we'll always miss

    //Various flags
    float suppload = 0;
    int min_core = -1;
    float min_core_load = 1;
    float max_core_load = 0;
    bool has_high_load = false;


    //Imbalance left after re-balancing
    float total_imbalance = 0;

    //Per-core load statistic
    std::vector<Load> load(rload.size(),Load());

    //Track physical CPU id to assigned ids
    std::vector<unsigned> map_phys_to_id(click_max_cpu_ids(), -1);

    //Per NUMA-socket load. When do_numa is false, we consider a single NUMA node
    std::vector<SocketLoad> sockets_load;
    bool do_numa = _numa;
    int numamax = do_numa?_numa_num:1;
    sockets_load.resize(numamax);

    //Keeps track of the whole machine load stats
    MachineLoad machine_load;

    //For each assigned cores, we compute the load and its smothed average
    //We check if some cores are completely overloaded, and report all that per-NUMA socket
    for (int j = 0; j < rload.size(); j++) {
        int physcpuid = rload[j].first;
        float load_current = rload[j].second;
        //assert(load_current <= 1);
        if (load_current < min_core_load) {
            min_core = j;
            min_core_load = load_current;
        }

        load[j].cpu_phys_id = physcpuid;

        if (_past_load[physcpuid] == 0)
            load[j].load = load_current;
        else
            load[j].load = load_current * _load_alpha + _past_load[physcpuid] * (1.0-_load_alpha);

        _past_load[physcpuid] = load[j].load;

        float diff = _target_load - load[j].load; // >0 if underloaded diff->quantity to add
        suppload += diff;
        if (abs(diff) <= _threshold)
            diff = 0;
        if (load[j].load > _target_load) {
            has_high_load = true;
            load[j].high = true;
        }
        if (load[j].load > max_core_load) {
            max_core_load = load[j].load;
        }
        map_phys_to_id[physcpuid] = j;

        machine_load.total_load += load[j].load;

        if (do_numa) {
#if HAVE_NUMA
            int numaid =  Numa::get_numa_node_of_cpu(load[j].cpu_phys_id);
#else
            int numaid = 0;
#endif
            sockets_load[numaid].N++;
            sockets_load[numaid].total_load += load[j].load;
        }
    }


    machine_load.N = load.size();
    machine_load.target = machine_load.total_load / (float)machine_load.N;

    for (int i = 0; i < sockets_load.size(); i++) {
        sockets_load[i].target = sockets_load[i].total_load / (float)sockets_load[i].N;
        if (do_numa && abs(sockets_load[i].target -  machine_load.target) > _threshold * 2) {
            click_chatter("Non-numa forced");
            do_numa=false;
        }
    }

    //suppload = (machine_load.N *_target_load) - totalload; //Total power that we have in excess

    float var_protect = (machine_load.N + 1) * (max(0.05f,(max_core_load-_target_load)));
    if (unlikely(balancer->verbose())) {
        click_chatter("Target %f. Total load %f. %d cores. Suppload %f, Var %f", machine_load.target, machine_load.total_load, machine_load.N, suppload, var_protect);
    }


    //Count the number of packets for each core
    for (int j = 0; j < _table.size(); j++) {
        unsigned long long c = get_node_count(j);
        unsigned cpu_phys_id =_table[j];
        if (unlikely(cpu_phys_id >= map_phys_to_id.size())) {
            click_chatter("ERROR : invalid phys id %d", cpu_phys_id);
            print_cpu_assign();
            abort();
        }
        unsigned cpuid = map_phys_to_id[cpu_phys_id];
        if (unlikely(cpuid >= load.size())) {
            click_chatter("ERROR : invalid cpu id %d for physical id %d, table index %d", cpuid, cpu_phys_id, j);
            print_cpu_assign();
            abort();
        }
        auto &l = load[cpuid];
        l.npackets += c;
        l.nbuckets += 1;
        if (c > 0)
            load[map_phys_to_id[_table[j]]].nbuckets_nz += 1;
    }


    //True if some buckets were moved
    bool moved = false;

    /**
     * Scaling. If we need more cores, we just add it and the imbalance mapping will do the rest
     * If we remove a core, we minimize the selection of buckets of the removed core -> existing cores to
     * diminish the imbalance already
     */
    if (balancer->autoscale()) {
        if (suppload > (1 + (1 - _target_load) + var_protect)  && machine_load.N > 1) { // we can remove a core
            if (unlikely(balancer->verbose())) {
                click_chatter("Removing a core (target %f)",_target_load);
            }

            float min_core_load = load[min_core].load;
            int min_core_phys = load[min_core].cpu_phys_id;

            click_chatter("Removing core %d (phys %d)", min_core, min_core_phys);
            unsigned long long totcount = load[min_core].npackets;
            load[min_core] = load[load.size() - 1];
            load.pop_back();
            balancer->removeCore(min_core_phys);

            //List of moves per-core, for manager
            std::vector<std::vector<std::pair<int,int>>> omoves(click_max_cpu_ids(), std::vector<std::pair<int,int>>());
            //Indexes of all buckets to remove (buckets of the removed core)
            std::vector<int> buckets_indexes;

            //Fill buckets_indexes
            for (int j = 0; j < _table.size(); j++) {
                if (_table[j] == min_core_phys) {
                    buckets_indexes.push_back(j);
                }
            }

            if (unlikely(balancer->verbose()))
                click_chatter("Removing %lu buckets of core %d", buckets_indexes.size(), min_core);

            BucketMapProblem cp(buckets_indexes.size(), load.size()); //load.size() is already fixed

            //Compute load for each buckets
            for (int i = 0; i < buckets_indexes.size(); i++) {
                int j = buckets_indexes[i];
                double c = get_node_count(j);
                cp.buckets_load[i] = (((double) c / (double)totcount)*min_core_load);
            }

            //Add imbalance of all cores
            std::vector<float> imbalance(machine_load.N-1,0);
            machine_load.N = machine_load.N-1;
            machine_load.target = machine_load.total_load / (float)machine_load.N;

            //Fix imbalance without the removed core
            for (int i = 0; i < machine_load.N; i++) {
                //Imbalance is positive if the core should loose some load
                imbalance[i] = 0 * (1.0-_imbalance_alpha) + ( (machine_load.target - load[i].load) * _imbalance_alpha); //(_last_imbalance[load[i].first] / 2) + ((p.target - load[i].second) / 2.0f);
            }

            click_chatter("Solving problem...");

            //Solve problem ; map all buckets of removed core to other cores
            total_imbalance = cp.solve();

            for (int i = 0; i < buckets_indexes.size(); i++) {
                unsigned j = buckets_indexes[i];
                unsigned raw_id = cp.transfer[i];
                //assert(raw_id != -1);
                unsigned cpu_phys_id = load[raw_id].cpu_phys_id;
                if (balancer->_manager) {
                    omoves[_table[j]].push_back(std::pair<int,int>(j, cpu_phys_id));
                }
                _table[j] = cpu_phys_id;
                //assert(cpu_phys_id != -1);
                //assert(cpu_phys_id != min_core_phys);
                //click_chatter("Bucket %d (%d) -> new id %d phys core %d", i ,j, raw_id, cpu_phys_id);
            }
            apply_moves([](int id){return id;},omoves,begin);
            moved = true;
            goto reset_count;
        } else if (suppload < -0.1) { //We need a new core because the total load even with perfect spread incurs 10% overload
            if (unlikely(balancer->verbose())) {
                click_chatter("Adding a core as load is %f", suppload);
            }
            int a_phys_id = balancer->addCore();
            if (a_phys_id == -1) {
                if (unlikely(balancer->verbose()))
                    click_chatter("Not enough cores...");
            } else {
                //imbalance.resize(imbalance.size() + 1);
                machine_load.N = machine_load.N+1;
                machine_load.target = machine_load.total_load / (float)machine_load.N;
                int aid = load.size();
                load.push_back(Load(a_phys_id));
                map_phys_to_id[a_phys_id] = aid;
                if (do_numa) {
                    //We disable numa in any case, to allow inter-numa shuffling when we add a core
                    //int numaid =  Numa::get_numa_node_of_cpu(a_phys_id);
                    //sockets_load[numaid].N++;
                    //sockets_load[numaid].target = sockets_load[numaid].total_load / (float)sockets_load[numaid].N;;
                    ////sockets_load[numaid].imbalance.resize(sockets_load[numaid].imbalance.size() + 1);
                    do_numa = false;
                }
            }
        }
    } else {
        if (machine_load.target <  _min_load && !_threshold_force_overload) {
            click_chatter("Underloaded, skipping balancing");
        }
    }

    //We need to fix the first numa socket if numa awareness has been disabled
    if (!do_numa) {
        sockets_load.resize(1);
        sockets_load[0].N = machine_load.N;
        sockets_load[0].target = machine_load.target;
        sockets_load[0].total_load = machine_load.total_load;
        numamax = 1;
    }


    /*
     * Dancers
     * We move the bucket that have more load than XXX 50% to other cores
     * Not shown in paper. Kept for later. Study of doing this VS splitting TC
     */
     if (has_high_load && _dancer) {
        if (unlikely(balancer->verbose()))
            click_chatter("Has high load !");
        for (int j = 0; j < _table.size(); j++) {
            unsigned long long c = get_node_count(j);
            unsigned phys_id = _table[j];
            unsigned id = map_phys_to_id[phys_id];
            if (!load[id].high || load[id].npackets == 0 || load[id].nbuckets <= 1)
                continue;
            unsigned long long pc = (c * 1024) / load[id].npackets;
            float l = ((float)pc / 1024.0) * (load[id].load);
            if (l > 0.5) {
                click_chatter("Bucket %d (cpu id %d) is a dancer ! %f%% of the load", j, id, l);

                float min_load = 1;
                int least = -1;
                for (int i = 0; i < load.size(); i++) {
                    if (load[i].load < min_load) {
                        min_load = load[i].load;
                        least = i;
                    }
                }

                if (unlikely(balancer->verbose()))
                    click_chatter("Moving to %d", least);

                //We fix the load here. So the next step of the algo will rebalance the other flows
                // as if nothing happened
                load[least].load += l;
                load[least].npackets += c;
                load[least].nbuckets+=1;
                load[least].nbuckets_nz+=1;
                load[id].load -= l;
                load[id].nbuckets-=1;
                load[id].nbuckets_nz-=1;
                load[id].npackets-= c;
                _table[j] = load[least].cpu_phys_id;
            }
        }
    }

    {
        //std::vector<int> machine_oid;
        //std::vector<int> machine_uid;

        //imbalance.resize(machine_load.N);
        //Prepare per-numa socket data structures
        for (int i = 0; i < numamax; i++) {
            SocketLoad &socket = sockets_load[i];
            std::vector<float> &imbalance = socket.imbalance;
            imbalance.resize(machine_load.N); //imbalance have "load" ids, not per-socket ids
            //click_chatter("Socket %d/%d has %d cores", i,numamax,socket.N);
        }

        int noverloaded = 0;
        int nunderloaded = 0;
        //Compute the imbalance
        for (unsigned i = 0; i < machine_load.N; i++) {
            int cpuid = load[i].cpu_phys_id;
            int numa = 0;
            if (do_numa) {
#if HAVE_NUMA
                numa = Numa::get_numa_node_of_cpu(cpuid);
#endif
            }
            SocketLoad &socket = sockets_load[numa];
            std::vector<float> &imbalance = socket.imbalance;

            if (_moved[cpuid]) {
                imbalance[i] = 0;
                _moved[cpuid] = false;
                //click_chatter("Cpu %d just moved",i);
                continue;
            }

            imbalance[i] = 0 * (1.0-_imbalance_alpha) + ( (socket.target - load[i].load) * _imbalance_alpha); //(_last_imbalance[load[i].first] / 2) + ((p.target - load[i].load) / 2.0f);i
            total_imbalance += abs(imbalance[i]);

            if (imbalance[i] > _threshold) {
                if (unlikely(balancer->verbose()))
                    click_chatter("Underloaded %lu is cpu %d, imb %f, buckets %d",socket.uid.size(),i, imbalance[i],load[i].nbuckets_nz);
                socket.uid.push_back(i);
                nunderloaded++;
            } else if (imbalance[i] < - _threshold) {
                if (load[i].nbuckets_nz == 0) {
                    click_chatter("WARNING : Core %d is overloaded but has no buckets !", i);
                } else if (load[i].nbuckets_nz > 1) { //Else there is nothing we can do
                   if (unlikely(balancer->verbose()))
                        click_chatter("Overloaded %lu is cpu %d, imb %f, buckets %d",socket.oid.size(),i, imbalance[i],load[i].nbuckets_nz);
                    socket.oid.push_back(i);
                    noverloaded++;
                } else {
                    if (unlikely(balancer->verbose()))
                        click_chatter("Overloaded cpu %d, imb %f, buckets %d",i, imbalance[i],load[i].nbuckets_nz);
                }
            } else {
                if (unlikely(balancer->verbose()))
                    click_chatter("Ignored cpu %d, imb %f, buckets %d",i, imbalance[i],load[i].nbuckets_nz);

            }
        }



        std::vector<std::vector<std::pair<int,int>>> omoves(load.size(), std::vector<std::pair<int,int>>());
        /**
         * Re-balancing
         * We minimize the overall imbalance by moving some buckets from overloaded cores to underloaded cores
         */

        for (int nid = 0; nid < numamax; nid++) {
            SocketLoad &socket = sockets_load[nid];
            std::vector<float> &imbalance = socket.imbalance;

            if (!(socket.oid.size() > 0 && socket.uid.size() > 0))
                continue;
                //Count the number of packets for this core
                /*
                 * We rebalance all buckets of overloaded to the set of underloaded
                 */
                unsigned long long all_overloaded_count = 0;
                for (int u = 0; u < socket.oid.size(); u++) {
                    all_overloaded_count += load[socket.oid[u]].npackets;
                }

                struct Bucket{
                    int oid_id;
                    int bucket_id;
                    int cpu_id;
                };

            std::vector<Bucket> buckets_indexes;
            std::vector<int> oid;
            for (int i = 0; i < socket.oid.size(); i++) {
                if (!do_numa
#if HAVE_NUMA
                        || Numa::get_numa_node_of_cpu(socket.oid[i]) == nid
#endif
                        ) {
                    oid.push_back(socket.oid[i]);
                }
            }
            std::vector<int> uid;
            for (int i = 0; i < socket.uid.size(); i++) {
                if (!do_numa
#if HAVE_NUMA
                        || Numa::get_numa_node_of_cpu(socket.uid[i]) == nid
#endif
                        ) {
                    uid.push_back(socket.uid[i]);
                }
            }
            for (int j = 0; j < _table.size(); j++) {
                if (get_node_count(j) == 0) continue;
                for (int u = 0; u < oid.size(); u++) {
                    if (map_phys_to_id[_table[j]] == oid[u]) {
                        int numa = 0;
                        if (do_numa) {
#if HAVE_NUMA
                            numa = Numa::get_numa_node_of_cpu(_table[j]);
#endif
                        }
                        buckets_indexes.push_back(Bucket{.oid_id =u,.bucket_id =j, .cpu_id = oid[u]});
                        break;
                    }
                }
            }

            if (buckets_indexes.size() == 0)
                continue;
            BucketMapTargetProblem pm(buckets_indexes.size(), uid.size(), oid.size());

            //Compute load for each buckets
            for (int i = 0; i < buckets_indexes.size(); i++) {
                Bucket& b = buckets_indexes[i];
                int j = b.bucket_id;
                double c = get_node_count(j);
                pm.buckets_load[i] = (((double) c / (double)load[b.cpu_id].npackets)*load[b.cpu_id].load);
                pm.buckets_max_idx[i] = b.oid_id;
                //click_chatter("Bucket %d (%d) load %f, %d packets", i ,j, pm.buckets_load[i], (uint64_t)c);
            }

            for (int i = 0; i < uid.size(); i++) {
                pm.target[i] = imbalance[uid[i]];
            }

            for (int i = 0; i < oid.size(); i++) {
                pm.max[i] = -imbalance[oid[i]];
            }

            pm.solve(balancer);

            for (int i = 0; i < pm.buckets_load.size(); i++) {
                Bucket& b = buckets_indexes[i];
                int to_uid = pm.transfer[i];
                if (to_uid == -1) continue;

                if (unlikely(balancer->verbose() > 2))
                    click_chatter("B idx %d to ucpu %d",i,to_uid);
                int to_cpu = uid[to_uid];
                int from_cpu = oid[pm.buckets_max_idx[i]];
                imbalance[from_cpu] += pm.buckets_load[i];
                imbalance[to_cpu] -= pm.buckets_load[i];

                if (unlikely(balancer->verbose()))
                    click_chatter("Move bucket %d from core %d to core %d",b.bucket_id,_table[b.bucket_id], load[to_cpu].cpu_phys_id);

                //If there is a manager, we must advertise the moves
                if (balancer->_manager) {
                    omoves[from_cpu].push_back(std::pair<int,int>(b.bucket_id, load[to_cpu].cpu_phys_id));
                }
                _table[b.bucket_id] = load[to_cpu].cpu_phys_id;

                _moved[load[to_cpu].cpu_phys_id] = true;
                moved = true;
            }

        }


        if (moved) {
            total_imbalance = 0;
            for (int nid = 0; nid < numamax; nid++) {
                SocketLoad &socket = sockets_load[nid];

                for (unsigned i = 0; i < socket.N; i++) {
                    total_imbalance += abs(socket.imbalance[i]);
                    if (abs(socket.imbalance[i]) > _threshold * 2 ) {
                        if (unlikely(balancer->verbose()))
                            click_chatter("Imbalance of core %d left to %f, that's quite a MISS.", i, socket.imbalance[i]);
                    }
                }
            }

            apply_moves([load](int id){return load[id].cpu_phys_id;}, omoves, begin);
        }
    }
reset_count:

    if (!_counter_is_xdp)
        ((AggregateCounterVector*)_counter)->advance_epoch();
    else {
#ifdef HAVE_BPF
        click_chatter("Reseting XDP table");
        int cpus = balancer->num_max_cpus();
        unsigned int nr_cpus = bpf_num_possible_cpus();
        uint64_t values[nr_cpus];
        bzero(values, sizeof(uint64_t) * nr_cpus);
        for (uint32_t key = 0; key < _count.size(); key++) {
            if (bpf_map_update_elem(_xdp_table_fd, &key, values, BPF_ANY)) {
                click_chatter("XDP set failed");
                continue;
            }
        }
#endif
    }


    //Change the speed of ticks if necessary
    if (total_imbalance > 0.2) {
        if (total_imbalance > 0.4)
            balancer->set_tick(balancer->tick_min());
        else
            balancer->set_tick(balancer->current_tick() / 2);
    } else if (!moved) {
        balancer->set_tick(balancer->current_tick() * 2);
    }

    if (balancer->current_tick() > balancer->tick_max())
        balancer->set_tick(balancer->tick_max());
    if (balancer->current_tick() < balancer->tick_min())
        balancer->set_tick(balancer->tick_min());
}
