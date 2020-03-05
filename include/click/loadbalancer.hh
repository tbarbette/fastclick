// -*- c-basic-offset: 4 -*-
#ifndef CLICK_LB_HH
#define CLICK_LB_HH

#include <click/hashtable.hh>
#include <click/ipflowid.hh>

class LoadBalancer { public:

    LoadBalancer() : _current(0), _mode_case(weight_round_robin) {
        modetrans.find_insert("rr",round_robin);
        modetrans.find_insert("hash",direct_hash);
        modetrans.find_insert("wrr",weight_round_robin);
    }

    enum LBMode {
        round_robin,
        weight_round_robin,
        direct_hash
    };

    HashTable<String, LBMode> modetrans;
    per_thread<int> _current;
    String _lb_mode;
    Vector <IPAddress> _dsts;
    Vector <int> _weights;
    LBMode _mode_case;

    void set_mode(String mode) {
        _lb_mode = mode;
        auto item = modetrans.find(_lb_mode);
        _mode_case = item.value();
    }

    int pick_server(const Packet* p) {
        switch(_mode_case) {
            case round_robin: {
                //click_chatter("Robin robin mode");
                int b = (*_current) ++;
                if (*_current == _dsts.size()) {
                    *_current = 0;
                    b = 0;
                }
                return b;
                break;
            }
            case direct_hash: {
                //click_chatter("Hash mode");
                unsigned server_val = IPFlowID(p, false).hashcode();
                server_val = ((server_val >> 16) ^ (server_val & 65535)) % _dsts.size();
                return server_val;
                break;
            }

            default: {
                //click_chatter("No mode set, go to bucket 0");
                return 0;
                break;
            }


        }

    }
};

#endif
