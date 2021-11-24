#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include "flowipmanager_cuckoopp_imp.hh"
#include <rte_hash.h>
#include <click/dpdk_glue.hh>
#include <rte_ethdev.h>

extern "C" {
        #include <cuckoopp/rte_hash_bloom.h>
}


CLICK_DECLS

int
FlowIPManager_CuckooPPIMP::alloc(int core)
{
    
    struct rte_hash_hvariant_parameters hash_params = {0};
    
    char buf[64];
    sprintf(buf, "%i-%s", core, name().c_str());

    hash_params.name = buf;
    hash_params.entries = _table_size;

    _tables[core].hash = rte_hash_bloom_create(&hash_params);
    if(unlikely(_tables[core].hash == nullptr))
    {
	VPRINT(0,"Could not init flow table %d!", core);
	return 1;
    }

    return 0;
}

int
FlowIPManager_CuckooPPIMP::find(IPFlow5ID &f, int core)
{
    auto& tab = _tables[core];

    auto *table = 	reinterpret_cast<rte_hash_hvariant *>(tab.hash);
    hash_key_t key = {0};
    key.a = ((uint64_t) f.saddr().addr() << 32) | ((uint64_t)f.daddr().addr());
    key.b = ((uint64_t) f.proto() << 32) | ((uint64_t)f.sport() << 16) | ((uint64_t)f.dport());

    hash_data_t data = {0};

    int ret = rte_hash_bloom_lookup_data(table, key, &data, 0);

    
    return ret >= 0 ? data.a: 0;
}

int
FlowIPManager_CuckooPPIMP::insert(IPFlow5ID &f, int flowid, int core)
{
    auto& tab = _tables[core];
    auto *table = reinterpret_cast<rte_hash_hvariant *> (tab.hash);
    hash_key_t key = {0};
    key.a = ((uint64_t) f.saddr().addr() << 32) | ((uint64_t)f.daddr().addr());
    key.b = ((uint64_t) f.proto() << 32) | ((uint64_t)f.sport() << 16) | ((uint64_t)f.dport());

    hash_data_t data = {0};

    data.a = flowid;
    int ret = rte_hash_bloom_add_key_data(table, key, data, 0, 0);

    return ret >= 0? flowid : 0;
}


int
FlowIPManager_CuckooPPIMP::get_count(int core)
{
    auto& tab = _tables[core];
    auto *table = reinterpret_cast<rte_hash_hvariant *> (tab.hash);
    return  rte_hash_bloom_size(table, 0);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowIPManager_CuckooPPIMP)
ELEMENT_MT_SAFE(FlowIPManager_CuckooPPIMP)
ELEMENT_REQUIRES(dpdk)
