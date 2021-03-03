#ifndef LIBNICSCHEDULER_METHODRSS_HH
#define LIBNICSCHEDULER_METHODRSS_HH 1

#include <rte_ethdev.h>
#include <rte_flow.h>

class MethodRSS : public BalanceMethodDevice { public:

    MethodRSS(NICScheduler* b, EthernetDevice* fd);
    ~MethodRSS();

    int initialize(ErrorHandler *errh, int startwith) override CLICK_COLD;
    void rebalance(std::vector<std::pair<int,float>> load) override;
    std::vector<unsigned> _table;
    virtual std::string name() override CLICK_COLD { return "rss"; }

    void cpu_changed() override;

    struct rte_eth_rss_conf _rss_conf;
    std::vector<rte_flow*> _flows;

    bool update_reta_flow(bool validate = false);
    bool update_reta(bool validate = false);

    bool _update_reta_flow;
    int _reta_size;
    bool _isolate;

    bool _use_group;
    bool _use_mark;
    int _epoch;

    //RSSVerifier* _verifier;
};
#endif
