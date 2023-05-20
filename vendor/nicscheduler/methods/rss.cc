/**
 * RSS base
 */
#if RTE_VERSION >= RTE_VERSION_NUM(21,8,0,0)
#define ETH_RSS_IPV4 RTE_ETH_RSS_IPV4
#define ETH_RSS_NONFRAG_IPV4_TCP RTE_ETH_RSS_NONFRAG_IPV4_TCP
#define ETH_RSS_NONFRAG_IPV4_UDP RTE_ETH_RSS_NONFRAG_IPV4_UDP
#endif

MethodRSS::MethodRSS(NICScheduler* b, EthernetDevice* fd) :
    BalanceMethodDevice(b,fd),
    //_verifier(0),
    _isolate(0), _use_group(true), _use_mark(false), _epoch(1) {
}


MethodRSS::~MethodRSS() {
}

int MethodRSS::initialize(ErrorHandler *errh, int startwith) {
    int reta = _fd->get_rss_reta_size(_fd);
    click_chatter("Actual reta size %d, target %d", reta, _reta_size);
    if (reta <= 0)
        return errh->error("Device not initialized or RSS is misconfigured");
    if (_fd->get_rss_reta)
        _table = _fd->get_rss_reta(_fd);

    _table.resize(_reta_size);

    //We update the default reta to 0 to be sure it works
    for (int i = 0; i < _table.size(); i++) {
        _table[i] = 0;
    }
    _fd->set_rss_reta(_fd, _table.data(), _table.size());
#if HAVE_DPDK
    if (_is_dpdk) {
        int port_id = ((DPDKEthernetDevice*)_fd)->get_port_id();

        _rss_conf.rss_key = (uint8_t*)CLICK_LALLOC(128);
        _rss_conf.rss_key_len = 128; //This is only a max
        if (rte_eth_dev_rss_hash_conf_get(port_id, &_rss_conf) != 0) {
            errh->warning("Could not get RSS configuration. Will use a default one.");
            _rss_conf.rss_key_len = 40;
            _rss_conf.rss_hf = ETH_RSS_IPV4 | ETH_RSS_NONFRAG_IPV4_TCP | ETH_RSS_NONFRAG_IPV4_UDP;
            for (int i = 0; i < 40; i++)
                _rss_conf.rss_key[i] = click_random();

        }


        struct rte_flow_error error;
        rte_eth_dev_stop(port_id);
        //rte_eth_promiscuous_disable(port_id);
        int res = rte_flow_isolate(port_id, _isolate, &error);
        if (res != 0)
            errh->warning("Warning %d : Could not set isolated mode because %s !",res,error.message);

        rte_eth_dev_start(port_id);
    }
#endif
    for (int i = 0; i < _table.size(); i++) {
        _table[i] = i % startwith;
    }

    _fd->set_rss_reta(_fd, _table.data(), _table.size());
    click_chatter("RSS initialized with %d CPUs and %lu buckets", startwith, _table.size());
    int err = BalanceMethodDevice::initialize(errh, startwith);
    if (err != 0)
        return err;

    _update_reta_flow = true;
    if (_is_dpdk) {
       if (!update_reta_flow(true)) {
            _update_reta_flow = false;
            if (_fd->set_rss_reta(_fd, _table.data(), _table.size()) != 0)
                return errh->error("Neither flow RSS or global RSS works to program the RSS table.");
       } else
           click_chatter("RETA update method is flow");
    } else {
        _update_reta_flow = false;
        if (_fd->set_rss_reta(_fd, _table.data(), _table.size()) != 0)
            return errh->error("Cannot program the RSS table.");
    }
    if (!_update_reta_flow)  {
        click_chatter("RETA update method is global");
    }

    return err;
}

void MethodRSS::rebalance(std::vector<std::pair<int,float>> load) {
    //update_reta();
}

void MethodRSS::cpu_changed() {
    int m =  balancer->num_used_cpus();
    std::vector<std::vector<std::pair<int,int>>> omoves(balancer->num_used_cpus(), std::vector<std::pair<int,int>>());
    /*std::vector<int> epochs;
    epochs.resize(max_cpus());*/


    for (int i = 0; i < _table.size(); i++) {
        int newcpu = balancer->get_cpu_info(i % m).id;
        if (balancer->_manager && newcpu!= _table[i]) {
            omoves[_table[i]].push_back(std::pair<int,int>(i, newcpu));
        }
        //epochs(_table[i]) =
        _table[i] = newcpu;
    }

    if (balancer->_manager) {
        for (int i = 0; i < m; i++) {
            if (omoves[i].size() > 0) {
                balancer->_manager->pre_migrate((EthernetDevice*)_fd, i, omoves[i]);
            }
        }
    }
    click_chatter("Migration info written. Updating reta.");
    update_reta();
    click_chatter("Post migration");
    if (balancer->_manager) {
        for (int i = 0; i < m; i++) {
            if (omoves[i].size() > 0) {
                balancer->_manager->post_migrate((EthernetDevice*)_fd, i);
            }
        }
    }
    click_chatter("Post migration finished");
}

#if HAVE_DPDK
inline rte_flow* flow_add_redirect(int port_id, int from, int to, bool validate, int priority = 0) {
        struct rte_flow_attr attr;
        memset(&attr, 0, sizeof(struct rte_flow_attr));
        attr.ingress = 1;
        attr.group = from;
        attr.priority =  priority;

        struct rte_flow_action action[2];
        struct rte_flow_action_jump jump;


        memset(action, 0, sizeof(struct rte_flow_action) * 2);
        action[0].type = RTE_FLOW_ACTION_TYPE_JUMP;
        action[0].conf = &jump;
        action[1].type = RTE_FLOW_ACTION_TYPE_END;
        jump.group=to;

        std::vector<rte_flow_item> pattern;
        rte_flow_item pat;
        pat.type = RTE_FLOW_ITEM_TYPE_ETH;
        pat.spec = 0;
        pat.mask = 0;
        pat.last = 0;
        pattern.push_back(pat);
        rte_flow_item end;
        memset(&end, 0, sizeof(struct rte_flow_item));
        end.type =  RTE_FLOW_ITEM_TYPE_END;
        pattern.push_back(end);

        struct rte_flow_error error;
        int res = 0;
        if (validate)
            res = rte_flow_validate(port_id, &attr, pattern.data(), action, &error);
        if (res == 0) {
#if RTE_FLOW_TIMING
            click_cycles_t start = click_get_cycles();
#endif
            struct rte_flow *flow = rte_flow_create(port_id, &attr, pattern.data(), action, &error);
#if RTE_FLOW_TIMING
            click_cycles_t end = click_get_cycles();
            click_chatter("Redirect rules in %f usec",((double)(end-start) * 1000000) / (double)cycles_hz() );
#endif
            click_chatter("Redirect from %d to %d success",from,to);
            return flow;
        } else {
            if (validate) {
                click_chatter("Rule did not validate.");
            }
            return 0;
        }
}

bool MethodRSS::update_reta_flow(bool validate) {
again:
    int port_id = ((DPDKEthernetDevice*)_fd)->port_id;
    if (validate && _use_group == 1) {
        click_chatter("Checking group support");
        if (flow_add_redirect(port_id, 0,1, validate) != 0) {
            click_chatter("Using flow groups !");
            _flows.resize(3, 0);
        } else {
            click_chatter("Could not create flow group rule. Will use rules on group 0. Error %d : %s",rte_errno,rte_strerror(rte_errno));
            _use_group = 0;
        }
    }

    struct rte_flow_error error;

    /**
     * If groups are supported, we use 3 tables. The first one to redirect to 2 and 3 so we can slowly update 3, then make the first one go to 3, then do the opposite, etc.
     */
    if (_use_group) {
        rte_flow* &old = _flows[ 1 + (_epoch % 2)];
        if (old) {
            rte_flow_destroy(port_id, old, &error);
        }
        struct rte_flow_attr attr;
        memset(&attr, 0, sizeof(struct rte_flow_attr));
        attr.ingress = 1;
        attr.group=2 + (_epoch % 2);

        struct rte_flow_action action[3];
        struct rte_flow_action_mark mark;
        struct rte_flow_action_rss rss;

        memset(action, 0, sizeof(action));
        memset(&rss, 0, sizeof(rss));

        int aid = 0;
        if (_use_mark) {
            action[0].type = RTE_FLOW_ACTION_TYPE_MARK;
            mark.id = _epoch;
            action[0].conf = &mark;
            ++aid;
        }

        action[aid].type = RTE_FLOW_ACTION_TYPE_RSS;
        assert(_table.size() > 0);
        uint16_t queue[_table.size()];
        for (int i = 0; i < _table.size(); i++) {
            queue[i] = _table[i];
            assert(_table[i] >= 0);
            //click_chatter("%d->%d",i,_table[i]);
        }
        rss.types = _rss_conf.rss_hf;
        rss.key_len = _rss_conf.rss_key_len;
        rss.queue_num = _table.size();
        rss.key = _rss_conf.rss_key;
        rss.queue = queue;
        rss.level = 0;
        rss.func = RTE_ETH_HASH_FUNCTION_DEFAULT;
        action[aid].conf = &rss;
        ++aid;
        action[aid].type = RTE_FLOW_ACTION_TYPE_END;
        ++aid;

        std::vector<rte_flow_item> pattern;
        //Ethernet

        rte_flow_item pat;
        pat.type = RTE_FLOW_ITEM_TYPE_ETH;
        pat.spec = 0;
        pat.mask = 0;
        pat.last = 0;
        pattern.push_back(pat);

        pat.type = RTE_FLOW_ITEM_TYPE_IPV4;

        pat.spec = 0;
        pat.mask = 0;

        pat.last = 0;
        pattern.push_back(pat);

        rte_flow_item end;
        memset(&end, 0, sizeof(struct rte_flow_item));
        end.type =  RTE_FLOW_ITEM_TYPE_END;
        pattern.push_back(end);

        int res = 0;
        if (validate) {
            res = rte_flow_validate(port_id, &attr, pattern.data(), action, &error);

            if (_use_mark && res) {
                click_chatter("Rule did not validate with mark. Trying again without mark. Error %d (DPDK errno %d : %s",res,rte_errno, rte_strerror(rte_errno));
                _use_mark = 0;
                goto again;
            }
        }
        if (!res) {
#if RTE_FLOW_TIMING
            Timestamp start = Timestamp::now_steady();
#endif
            struct rte_flow *flow = rte_flow_create(port_id, &attr, pattern.data(), action, &error);

#if RTE_FLOW_TIMING
            Timestamp end = Timestamp::now_steady();
            click_chatter("In %d nsec",(end-start).nsecval());
#endif
            if (flow) {
                if (unlikely(balancer->verbose())) {
                    click_chatter("Flow added succesfully with %d patterns!", pattern.size());
                    if (validate) {
                        click_chatter("Mark enabled!");
                    }
                }
                old = flow;
                rte_flow* r1 = flow_add_redirect(port_id, 1,  2 + (_epoch % 2), validate, _epoch % 2);
                if (_flows[0])
                    rte_flow_destroy(port_id,_flows[0],&error);
                _flows[0] = r1;
            } else {
                if (unlikely(balancer->verbose()))
                    click_chatter("Could not add pattern with %d patterns, error %d : %s", pattern.size(),  res, error.message);
                    return false;
            }
        }

    } else {

        bool _use_prio = true;
        std::vector<rte_flow*> newflows;

        int tot = 1;
        if (!_use_prio) {
            if (_flows.size() == 1)
                tot = 2;
        }

        struct rte_flow_attr attr;
        for (int i = 0; i < tot; i++) {
            memset(&attr, 0, sizeof(struct rte_flow_attr));
            attr.ingress = 1;
            if (_use_prio) {
                attr.priority = _epoch % 2;
            }

            struct rte_flow_action action[3];
            struct rte_flow_action_mark mark;
            struct rte_flow_action_rss rss;

            memset(action, 0, sizeof(action));
            memset(&rss, 0, sizeof(rss));

            int aid = 0;
            if (_use_mark) {
                action[0].type = RTE_FLOW_ACTION_TYPE_MARK;
                mark.id = _epoch;
                action[0].conf = &mark;
                ++aid;
            }

            action[aid].type = RTE_FLOW_ACTION_TYPE_RSS;
            assert(_table.size() > 0);
            uint16_t queue[_table.size()];
            for (int i = 0; i < _table.size(); i++) {
                queue[i] = _table[i];
                assert(_table[i] >= 0);
                //click_chatter("%d->%d",i,_table[i]);
            }
            rss.types = _rss_conf.rss_hf;
            rss.key_len = _rss_conf.rss_key_len;
            rss.queue_num = _table.size();
            rss.key = _rss_conf.rss_key;
            rss.queue = queue;
            rss.level = 0;
            rss.func = RTE_ETH_HASH_FUNCTION_DEFAULT;
            action[aid].conf = &rss;
            ++aid;
            action[aid].type = RTE_FLOW_ACTION_TYPE_END;
            ++aid;

            std::vector<rte_flow_item> pattern;
            //Ethernet
            /*
            struct rte_flow_item_eth* eth = (struct rte_flow_item_eth*) malloc(sizeof(rte_flow_item_eth));
            struct rte_flow_item_eth* mask = (struct rte_flow_item_eth*) malloc(sizeof(rte_flow_item_eth));
            bzero(eth, sizeof(rte_flow_item_eth));
            bzero(mask, sizeof(rte_flow_item_eth));*/
            rte_flow_item pat;
            pat.type = RTE_FLOW_ITEM_TYPE_ETH;
            pat.spec = 0;
            pat.mask = 0;
            pat.last = 0;
            pattern.push_back(pat);

            pat.type = RTE_FLOW_ITEM_TYPE_IPV4;

           if (!_use_prio && tot == 2) {
               struct rte_flow_item_ipv4* spec = (struct rte_flow_item_ipv4*) malloc(sizeof(rte_flow_item_ipv4));
               struct rte_flow_item_ipv4* mask = (struct rte_flow_item_ipv4*) malloc(sizeof(rte_flow_item_ipv4));
               bzero(spec, sizeof(rte_flow_item_ipv4));
               bzero(mask, sizeof(rte_flow_item_ipv4));
               spec->hdr.dst_addr = i;
               mask->hdr.dst_addr = 1;
               pat.spec = spec;
               pat.mask = mask;
           } else {
               pat.spec = 0;
               pat.mask = 0;
           }

           pat.last = 0;
           pattern.push_back(pat);

            rte_flow_item end;
            memset(&end, 0, sizeof(struct rte_flow_item));
            end.type =  RTE_FLOW_ITEM_TYPE_END;
            pattern.push_back(end);

            struct rte_flow_error error;
            int res = 0;
            if (validate) {
                res = rte_flow_validate(port_id, &attr, pattern.data(), action, &error);

                if (_use_mark && res) {
                    click_chatter("Rule did not validate with mark. Trying again without mark. Error %d (DPDK errno %d : %s",res,rte_errno, rte_strerror(rte_errno));
                    _use_mark = 0;
                    goto again;
                }
            }
            if (!res) {

                struct rte_flow *flow = rte_flow_create(port_id, &attr, pattern.data(), action, &error);
                if (flow) {
                    if (unlikely(balancer->verbose()))
                        click_chatter("Flow added succesfully with %d patterns!", pattern.size());
                        if (validate) {
                            click_chatter("Mark enabled!");
                        }
                    } else {
                    if (unlikely(balancer->verbose()))
                        click_chatter("Could not add pattern with %d patterns, error %d : %s", pattern.size(),  res, error.message);
                    return false;
                }

                newflows.push_back(flow);
            } else {
            if (unlikely(balancer->verbose()))
                click_chatter("Could not validate pattern with %d patterns, error %d : %s", pattern.size(),  res, error.message);
                return false;
            }
         }

         while (!_flows.empty()) {
            struct rte_flow_error error;
            rte_flow_destroy(port_id,_flows.back(), &error);
            _flows.pop_back();
         }
         _flows = newflows;
    }
     //click_chatter("Epoch is %d", _epoch);
     _epoch ++;
     return true;

}
#endif
bool MethodRSS::update_reta(bool validate) {
    Timestamp t = Timestamp::now_steady();
    /*if (_verifier) {
        _verifier->_table = _table;
    }*/

    if (_update_reta_flow) {
        if (!update_reta_flow(validate))
            return false;
    } else {
        if (!_fd->set_rss_reta(_fd, _table.data(), _table.size()))
            return false;
    }

    Timestamp s = Timestamp::now_steady();
    if (validate || balancer->verbose()) {
        click_chatter("Reta updated in %ld usec",(s-t).usecval());
    }
    return true;
}
