// -*- c-basic-offset: 4 -*-
#ifndef CLICK_LB_HH
#define CLICK_LB_HH

#include <click/ipflowid.hh>
#include <algorithm>
#if HAVE_DPDK
#include <click/dpdk_glue.hh>
#endif
#include <click/hashtable.hh>
#include <click/ipflowid.hh>
#include <click/tcphelper.hh>
#include <click/straccum.hh>

class LoadBalancer { public:

    LoadBalancer() : _current(0), _dsts(), _weights_helper(), _mode_case(round_robin) {
        modetrans.find_insert("rr",round_robin);
        modetrans.find_insert("hash",direct_hash);
        modetrans.find_insert("hash_ip",direct_hash_ip);
#if HAVE_DPDK
        modetrans.find_insert("hash_crc",direct_hash_crc);
#endif
        modetrans.find_insert("hash_agg",direct_hash_agg);
        modetrans.find_insert("cst_hash_agg",constant_hash_agg);
        modetrans.find_insert("wrr",weighted_round_robin);
        modetrans.find_insert("awrr",auto_weighted_round_robin);
        modetrans.find_insert("least",least_load);
        modetrans.find_insert("pow2",pow2);
        lsttrans.find_insert("conn",connections);
        lsttrans.find_insert("packets",packets);
        lsttrans.find_insert("bytes",bytes);
        lsttrans.find_insert("cpu",cpu);
    }

    enum LBMode {
        round_robin,
        weighted_round_robin,
        auto_weighted_round_robin,
        pow2,
        constant_hash_agg,
        direct_hash,
        direct_hash_crc,
        direct_hash_agg,
	direct_hash_ip,
        least_load
    };

    static bool isLoadBased(LBMode mode) {
        return mode == pow2 || mode == least_load || mode == weighted_round_robin || mode == auto_weighted_round_robin;
    }

    // Metric to use when the LB technique is load-based
    enum LSTMode {
        connections,
        packets,
        bytes,
        cpu
    };

    typedef atomic_uint64_t load_type_t;

    struct load {
        load() {
            connection_load = 0;
            cpu_load = 0;
            packets_load = 0;
            bytes_load = 0;
        }
        load_type_t connection_load;
        load_type_t packets_load;
        load_type_t bytes_load;
        uint64_t cpu_load;
    } CLICK_CACHE_ALIGN;

protected:
    HashTable<String, LBMode> modetrans;
    HashTable<String, LSTMode> lsttrans;
    per_thread<int> _current;
    Vector <IPAddress> _dsts;
    unprotected_rcu_singlewriter<Vector <unsigned>,2> _weights_helper;
    LBMode _mode_case;
    LSTMode _lst_case;
    Vector <load,CLICK_CACHE_LINE_SIZE> _loads;
    Vector <unsigned> _selector;
    Vector <unsigned> _cst_hash;
    Vector <unsigned> _spares;
    bool _track_load;
    bool _force_track_load;
    int _awrr_interval;
    float _alpha;

    uint64_t get_load_metric(int idx) {
        return get_load_metric(idx, _lst_case);
    }
    uint64_t get_load_metric(int idx,LSTMode metric) {
        load& l = _loads[idx];
        switch(metric) {
            case connections: {
                return l.connection_load;
            }
            case bytes: {
                return l.bytes_load;
            }
            case packets: {
                return l.packets_load;
            }
            case cpu: {
                return l.cpu_load;
            }
            default:
                assert(false);
        }
    }

    inline void track_load(Packet*p, int b) {
	if (_track_load) {
	    if (TCPHelper::isSyn(p))
                 _loads[b].connection_load++;
	    else if (TCPHelper::isFin(p) || TCPHelper::isRst(p))
                 _loads[b].connection_load--;

            _loads[b].packets_load++;
            _loads[b].bytes_load += p->length();
        }
    }

    unsigned cantor(unsigned a, unsigned b) {
        return ((a + b)  * (a + b + 1))/2 + b;
    }

    /* Builds the constant hashing map
     */
    void build_hash_ring() {
        Vector<unsigned> new_hash;
        new_hash.resize(_cst_hash.size(), -1);
        int fac = ((new_hash.size() - 1) / _selector.size()) + 1;
        for (int j = 0; j < fac; j++) {
            for (int i = 0; i < _selector.size(); i++) {
                int server_place = cantor(_selector[i], j) % new_hash.size();
                new_hash[server_place] = _selector[i];
            }

        }
        int cur = _selector[0];
        for (int i = 0; i < new_hash.size(); i++) {
            if (new_hash[i] == - 1) {
                new_hash[i] = cur;
            } else
                cur = new_hash[i];
        }
        _cst_hash.swap(new_hash);
    }

    static void atc(Timer *timer, void *user_data) {
        LoadBalancer* lb = (LoadBalancer*)user_data;
        uint64_t metric_tot = 0;
        for (int i = 0; i < lb->_loads.size(); i++) {
            metric_tot += lb->get_load_metric(i);
        }
        uint64_t avg = metric_tot / lb->_dsts.size();

        Vector<unsigned> new_weight;
        for (int i = 0; i < lb->_dsts.size(); i++) {
                float l = lb->get_load_metric(i);
                /**
                 *Examples:
                 * Average is 50. Load of this core is 80.
                 * a=-1 -> 10*50 / 160 - 50 = 4,5
                 * Load is 30.
                 * a=0 -> 10 * 50 / 30 = 17
                 * a=-1 -> 10 * 50 / 10 = 5
                 * Load is 50
                 * a=-1 -> 10 *50 / 2*50 - 50 = 10
                 */
                float buckets;
                if (l == 0) {
                    buckets = 30;
                } else {
                buckets = 10 * avg / ((1 - lb->_alpha) * l + lb->_alpha*avg);
                if (buckets > 30) buckets = 30;
                else if (buckets < 2) buckets = 2;
                }
                new_weight.push_back((unsigned)buckets);
               // click_chatter("%d load %f,  %d buck",i,l,(unsigned)buckets);
        }
        lb->set_weights(new_weight.data());
        timer->reschedule_after_msec(lb->_awrr_interval);
    }

    int hash_ip(const Packet* p) {
        const unsigned char *data = p->data();
        int o = 32, l = 8;
        int h;
        if ((int)p->length() < o + l)
            h = 0;
        else {
            int d = 0;
            for (int i = o; i < o + l; i++)
                d += data[i];
            int n = _selector.size();
            if (n == 2 || n == 4 || n == 8)
                h = (d ^ (d>>4)) & (n-1);
            else
              h = (d % n);
        }
        return h;
    }


    // Parse common LB parameters
    int parseLb(Vector<String> &conf, Element* lb, ErrorHandler* errh) {
        String lb_mode;
        String lst_mode;
        int awrr_timer;
        double alpha;
        bool has_cst_buckets;
        int cst_buckets;
        int nserver;
	bool force_track_load;
        int ret = Args(lb, errh).bind(conf)
            .read_or_set("LB_MODE", lb_mode,"rr")
            .read_or_set("LST_MODE",lst_mode,"conn")
            .read_or_set("AWRR_TIME",awrr_timer, 100)
            .read_or_set("FORCE_TRACK_LOAD", force_track_load, false)
            .read_or_set("NSERVER", nserver, 0)
            .read("CST_BUCKETS", cst_buckets).read_status(has_cst_buckets)
            .read_or_set("AWRR_ALPHA", alpha, 0).consume();

        if (ret < 0)
            return -1;

        _alpha = alpha;
	_force_track_load = force_track_load;
        if (has_cst_buckets) {
            _cst_hash.resize(cst_buckets, -1);
        }

        set_mode(lb_mode, lst_mode, lb, awrr_timer, nserver);

        return ret;
    }

   enum {
            h_load,h_nb_total_servers,h_nb_active_servers,h_load_conn,h_load_packets,h_load_bytes,h_add_server,h_remove_server
    };


    int lb_handler(int op, String &data, void *r_thunk, void* w_thunk, ErrorHandler *errh) {

    LoadBalancer *cs = this;
    if (op == Handler::f_read) {
        switch((uintptr_t) r_thunk) {
           case h_load: {
                StringAccum acc;
		if (data) {
		    int i = atoi(data.c_str());
                    if (cs->_loads.size() <= i) {
		        acc << "unknown";
		    } else {
		        acc << cs->_loads[i].cpu_load ;
		    }
		} else {
		    for (int i = 0; i < cs->_dsts.size(); i ++) {
			if (cs->_loads.size() <= i) {
			    acc << "unknown";
			} else {
			    acc << cs->_loads[i].cpu_load ;
			}
			acc << (i == cs->_dsts.size() -1?"":" ");
		    }
		}
                data = acc.take_string();
		return 0;
            }
	}
    } else {
       switch((uintptr_t)w_thunk) {
            case h_load: {
                String s(data);
                //click_chatter("Input %s", s.c_str());
                while (s.length() > 0) {
                    int ntoken = s.find_left(',');
                    if (ntoken < 0)
                        ntoken = s.length() - 1;
                    int pos = s.find_left(':');
                    int server_id = atoi(s.substring(0,pos).c_str());
                    int server_load = atoi(s.substring(pos + 1, ntoken).c_str());
                    //click_chatter("%d is %d",server_id, server_load);
                    if (cs->_loads.size() <= server_id) {
                        click_chatter("Invalid server id %d", server_id);
                        return 1;
                    }
                    cs->_loads[server_id].cpu_load = server_load;
                    s = s.substring(ntoken + 1);
                }

                return 0;
            }
	}
      }
    }


    int lb_write_handler(
            const String &input, void *thunk, ErrorHandler *errh) {
        LoadBalancer *cs = this;
        switch((uintptr_t) thunk) {
            case h_add_server: {
                //add_server();
                break;
            }
            case h_remove_server: {
                //remove_server();
                break;
            }
        }
        return -1;
    }

    String
    lb_read_handler(void *thunk) {
        LoadBalancer *cs = this;

        switch((uintptr_t) thunk) {
            case h_nb_active_servers: {
               return String(cs->_selector.size());
            }
            case h_nb_total_servers: {
                return String(cs->_dsts.size());
            }

            case h_load_conn: {
                StringAccum acc;
                for (int i = 0; i < cs->_dsts.size(); i ++) {
                    acc << cs->get_load_metric(i,connections) << (i == cs->_dsts.size() -1?"":" ");
                }
                return acc.take_string();}
            case h_load_packets:{
                StringAccum acc;
                for (int i = 0; i < cs->_dsts.size(); i ++) {
                    acc << cs->get_load_metric(i,packets) << (i == cs->_dsts.size() -1?"":" ");
                }
                return acc.take_string();}
            case h_load_bytes:{
                StringAccum acc;
                for (int i = 0; i <cs-> _dsts.size(); i ++) {
                    acc << cs->get_load_metric(i,bytes) << (i == cs->_dsts.size() -1?"":" ");
                }
                return acc.take_string();}
            default:
                return "<none>";
        }
    }

    template <class T>
    void add_lb_handlers(Element* _e) {
	T* e = (T*)_e;
	e->set_handler("load", Handler::f_read | Handler::f_read_param | Handler::f_write, e->handler, h_load, h_load);
	e->add_read_handler("nb_active_servers", e->read_handler, h_nb_active_servers);
	e->add_read_handler("nb_total_servers", e->read_handler, h_nb_total_servers);
	e->add_read_handler("load_conn", e->read_handler, h_load_conn);
	e->add_read_handler("load_bytes", e->read_handler, h_load_bytes);
	e->add_read_handler("load_packets", e->read_handler, h_load_packets);
	//e->add_write_handler("remove_server", e->write_handler, h_remove_server);
	//e->add_write_handler("add_server", e->write_handler, h_add_server);
    }

    void set_mode(String mode, String metric="cpu", Element* owner=0,int awrr_timer_interval = -1, int nserver = 0) {
        auto item = modetrans.find(mode);
        _mode_case = item.value();
        if (_mode_case == weighted_round_robin || _mode_case == auto_weighted_round_robin) {
            auto &wh = _weights_helper.write_begin();
            wh.resize(_dsts.size());
            for(int i=0; i<_dsts.size(); i++) {
                wh[i] = i;
            }
            _weights_helper.write_commit();
        }

        _lst_case = lsttrans.find(metric).value();
        _track_load = ((isLoadBased(_mode_case)) && _lst_case != cpu) || _force_track_load;

        if (_mode_case == auto_weighted_round_robin) {
            Timer* awrr_timer = new Timer(atc, this);
            awrr_timer->initialize(owner, false);
            _awrr_interval = awrr_timer_interval;
            awrr_timer->schedule_after(Timestamp::make_msec(_awrr_interval));
        }

        if (nserver == 0) {
            nserver= _dsts.size();
        }

        if (_mode_case == round_robin ||_mode_case == weighted_round_robin || _mode_case == auto_weighted_round_robin) {
            int p = nserver / _current.weight();
            if (p == 0)
                p = 1;
            for (int i = 0; i < _current.weight(); i++) {
                _current.get_value(i) = (i * p) % nserver;
            }
        }
        _spares.reserve(_dsts.size());
        _selector.reserve(_dsts.size());
        for (int i = nserver; i < _dsts.size(); i++)
            _spares.push_back(i);
        for (int i = 0; i < nserver; i++)
            _selector.push_back(i);

        if (_mode_case == constant_hash_agg) {
            if (_cst_hash.size() == 0)
                _cst_hash.resize(_dsts.size() * 100);
            build_hash_ring();
        }

        _loads.resize(_dsts.size());
        CLICK_ASSERT_ALIGNED(_loads.data());

    }

    void set_weights(unsigned weigths_value[]) {
        Vector<unsigned> weights_helper;
        for(int i=0; i<_dsts.size(); i++) {
            for (unsigned j=0; j<weigths_value[i]; j++) {
                weights_helper.push_back(i);
            }
        }
        std::random_shuffle(weights_helper.begin(), weights_helper.end());
        auto& v = _weights_helper.write_begin();
        v = weights_helper;
        _weights_helper.write_commit();
    }

    inline int pick_server(const Packet* p) {
        switch(_mode_case) {
            case round_robin: {
                int b = _selector.unchecked_at((*_current)++);
                if (*_current == (unsigned)_selector.size()) {
                    *_current = 0;
                }
                return b;
            }
            case direct_hash_crc: {
                IPFlow5ID srv = IPFlow5ID(p);
                unsigned server_val = ipv4_hash_crc(&srv, sizeof(srv), 0);
                server_val = ((server_val >> 16) ^ (server_val & 65535)) % _selector.size();
                return _selector.unchecked_at(server_val);
            }
            case direct_hash_agg: {
                unsigned server_val = AGGREGATE_ANNO(p);
                server_val = ((server_val >> 16) ^ (server_val & 65535)) % _selector.size();
                return _selector.unchecked_at(server_val);
            }
            case direct_hash_ip: {
                unsigned server_val = hash_ip(p);
		return _selector.unchecked_at(server_val);
            }
            case direct_hash: {
                unsigned server_val = IPFlowID(p, false).hashcode();
                server_val = ((server_val >> 16) ^ (server_val & 65535)) % _selector.size();
                return _selector.unchecked_at(server_val);
            }
            case constant_hash_agg: {
                unsigned server_val = AGGREGATE_ANNO(p);
                server_val = ((server_val >> 16) ^ (server_val & 65535)) % _cst_hash.size();
                return _cst_hash.unchecked_at(server_val);
            }
            case auto_weighted_round_robin:
            case weighted_round_robin: {
                //click_chatter("weighted Round Robin mode");
                auto & wh = _weights_helper.read_begin();
                int b = (*_current)++;
                if (*_current >= wh.size())
                    *_current = 0;
                if (b >= wh.size()) //Upon WR change, this may be over the new limit
                    b = 0;

                int server = wh.unchecked_at(b);
                _weights_helper.read_end();
                return server;
            }
            case least_load: {
                //click_chatter("Least loaded mode");
                std::function<bool(load,load)> comp;

                switch(_lst_case) {
                    case connections: {
                        comp = [&](load m,load n)-> bool {return m.connection_load<n.connection_load;};
                        break;
                    }
                    case bytes: {
                        comp = [&](load m,load n)-> bool {return m.bytes_load<n.bytes_load;};
                        break;
                    }
                    case packets: {
                        comp = [&](load m,load n)-> bool {return m.packets_load<n.packets_load;};
                        break;
                    }
                    case cpu: {
                        comp = [&](load m,load n)-> bool {return m.cpu_load<n.cpu_load;};
                        break;
                        /*Vector <uint64_t> load_dic;
                        for (int i=0;i<_dsts.size();i++){
                            dic.find_insert(_loads[i].cpu_load,i);
                            load_dic.push_back(_loads[i].cpu_load);
                        }
                        std::make_heap (load_dic.begin(), load_dic.end());
                        std::sort_heap(load_dic.begin(), load_dic.end());
                        return dic.find(load_dic.front()).value();*/
                    }
                    default:
                        assert(false);
                        break;
                }
                auto result = std::min_element(std::begin(_loads), std::end(_loads),comp);
                int sid = std::distance(std::begin(_loads), result);
                //click_chatter("%s\n--> %d",a.c_str(), sid);
                return sid;
            }
            case pow2: {
                //click_chatter("Power of 2 mode");
                int a = _selector.unchecked_at(click_random() % _selector.size());
                int b = _selector.unchecked_at(click_random() % _selector.size());
                switch(_lst_case) {
                    case connections: {
                        return (_loads[a].connection_load > _loads[b].connection_load?b:a);
                    }
                    case bytes: {
                        return (_loads[a].bytes_load > _loads[b].bytes_load?b:a);
                    }
                    case packets: {
                        return (_loads[a].packets_load > _loads[b].packets_load?b:a);
                    }
                    case cpu: {
                        int ret = (_loads[a].cpu_load > _loads[b].cpu_load?b:a);
                        //click_chatter("A %d B %d -> %d",a ,b, ret);
                        return ret;
                    }
                    default:
                        assert(false);
                }
                return -1; //unreachable
            }
            default: {
                //click_chatter("No mode set, go to bucket 0");
                return 0;
                break;
            }
        } //switch _lb_mode
    }
};

#endif
