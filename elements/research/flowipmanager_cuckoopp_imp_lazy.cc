#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include "flowipmanager_cuckoopp_imp_lazy.hh"
#include <rte_hash.h>
#include <click/dpdk_glue.hh>
#include <rte_ethdev.h>

extern "C" {
        #include <cuckoopp/rte_hash_lazy_bloom.h>
}


CLICK_DECLS

int
FlowIPManager_CuckooPPIMP_lazy::alloc(int core)
{
    
    struct rte_hash_hvariant_parameters hash_params = {0};
    
    char buf[64];
    sprintf(buf, "%i-%s", core, name().c_str());


    hash_params.name = buf;
    hash_params.entries = _table_size;
    _lazy_timeout  = _timeout_ms / _recycle_interval_ms;

    _tables[core].hash = rte_hash_lazy_bloom_create(&hash_params);
    if(unlikely(_tables[core].hash == nullptr))
    {
	VPRINT(0,"Could not init flow table %d!", core);
	return 1;
    }
    if(!_timer)
	_timer = new Timer(this);

    if(_timer && !_timer->initialized())
    {
        _timer->initialize(this, true);
        VPRINT(0,"Updating recycle epoch every %d ms", _recycle_interval_ms);
        _timer->schedule_after_msec(_recycle_interval_ms);
    }
#if IMP_COUNTERS
    if(_lazytables == 0)
    {
	  VPRINT(1,"Initialized lazy counters table");
	  _lazytables = CLICK_ALIGNED_NEW(lazytable, _tables_count);
	  CLICK_ASSERT_ALIGNED(_lazytables);
    }
	
#endif

    return 0;
}

int
FlowIPManager_CuckooPPIMP_lazy::find(IPFlow5ID &f, int core)
{
    auto& tab = _tables[core];
    auto *table = 	reinterpret_cast<rte_hash_hvariant *>(tab.hash);
    hash_key_t key = {0};
    key.a = ((uint64_t) f.saddr().addr() << 32) | ((uint64_t)f.daddr().addr());
    key.b = ((uint64_t) f.proto() << 32) | ((uint64_t)f.sport() << 16) | ((uint64_t)f.dport());

    hash_data_t data = {0};

    int ret = rte_hash_lazy_bloom_lookup_data(table, key, &data, _recent);
    
    return ret >= 0 ? data.a: 0;
}

int
FlowIPManager_CuckooPPIMP_lazy::insert(IPFlow5ID &f, int flowid, int core)
{
    auto& tab = _tables[core];
    auto *table = reinterpret_cast<rte_hash_hvariant *> (tab.hash);
    hash_key_t key = {0};
    key.a = ((uint64_t) f.saddr().addr() << 32) | ((uint64_t)f.daddr().addr());
    key.b = ((uint64_t) f.proto() << 32) | ((uint64_t)f.sport() << 16) | ((uint64_t)f.dport());

    hash_data_t data = {0};

    data.a = flowid;
    int ret = rte_hash_lazy_bloom_add_key_data(table, key, data, _recent + _lazy_timeout, _recent);

    //TODO: how to recognize insert from recycling??
    //TODO: LAZY counters
    return ret >= 0? flowid : 0;
}

void FlowIPManager_CuckooPPIMP_lazy::run_timer(Timer *t)
{
    _recent = click_jiffies() /  _recycle_interval_ms;
    _timer->schedule_after_msec(_recycle_interval_ms);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowIPManager_CuckooPPIMP_lazy)
ELEMENT_MT_SAFE(FlowIPManager_CuckooPPIMP_lazy)
ELEMENT_REQUIRES(dpdk)
