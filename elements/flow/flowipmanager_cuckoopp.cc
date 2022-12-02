#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include "flowipmanager_cuckoopp.hh"
#include <rte_hash.h>
#include <click/dpdk_glue.hh>
#include <rte_ethdev.h>

extern "C" {
        #include <cuckoopp/rte_hash_bloom.h>
}


CLICK_DECLS


int FlowIPManager_CuckooPP::configure(Vector<String> &conf, ErrorHandler *errh) {
    Args args(conf, this, errh);

    if (parse(&args) || args.complete())
        return errh->error("Error while parsing arguments!");

    find_children(_verbose);
    router()->get_root_init_future()->postOnce(&_fcb_builded_init_future);
    _fcb_builded_init_future.post(this);

    _reserve += reserve_size();

    return 0;
}

int
FlowIPManager_CuckooPP::alloc(FlowIPManager_CuckooPPState & table, int core, ErrorHandler* errh)
{
    struct rte_hash_hvariant_parameters hash_params = {0};

    char buf[64];
    sprintf(buf, "%i-%s", core, name().c_str());

    hash_params.name = buf;
    hash_params.entries = _capacity;

    table.hash = rte_hash_bloom_create(&hash_params);
    if(unlikely(table.hash == nullptr))
    {
	    click_chatter("Could not init flow table %d!", core);
	    return 1;
    }

    return 0;
}

int
FlowIPManager_CuckooPP::find(IPFlow5ID &f)
{
    auto *table = 	reinterpret_cast<rte_hash_hvariant *>(_tables->hash);
    hash_key_t key = {0};
    key.a = ((uint64_t) f.saddr().addr() << 32) | ((uint64_t)f.daddr().addr());
    key.b = ((uint64_t) f.proto() << 32) | ((uint64_t)f.sport() << 16) | ((uint64_t)f.dport());

    hash_data_t data = {0};

    int ret = rte_hash_bloom_lookup_data(table, key, &data, 0);

    return ret >= 0 ? data.a: 0;
}


int
FlowIPManager_CuckooPP::count()
{    
    int total = 0;
    for (int i = 0; i < _tables.weight(); i++) {
        auto *table = reinterpret_cast<rte_hash_hvariant *>(_tables.get_value(i).hash);
        total += rte_hash_bloom_size(table, 0);
    }

    return total;
}

int
FlowIPManager_CuckooPP::insert(IPFlow5ID &f, int flowid)
{
    auto *table = reinterpret_cast<rte_hash_hvariant *> (_tables->hash);
    hash_key_t key = {0};
    key.a = ((uint64_t) f.saddr().addr() << 32) | ((uint64_t)f.daddr().addr());
    key.b = ((uint64_t) f.proto() << 32) | ((uint64_t)f.sport() << 16) | ((uint64_t)f.dport());

    hash_data_t data = {0};

    data.a = flowid;
    int ret = rte_hash_bloom_add_key_data(table, key, data, 0, 0);

    return ret >= 0? flowid : -1;
}

int
FlowIPManager_CuckooPP::remove(IPFlow5ID &f)
{
    auto *table = reinterpret_cast<rte_hash_hvariant *> (_tables->hash);
    hash_key_t key = {0};
    key.a = ((uint64_t) f.saddr().addr() << 32) | ((uint64_t)f.daddr().addr());
    key.b = ((uint64_t) f.proto() << 32) | ((uint64_t)f.sport() << 16) | ((uint64_t)f.dport());

    int ret = rte_hash_bloom_del_key(table, key, 0);

    return ret >= 0? 0 : ret;
}

void FlowIPManager_CuckooPP::cleanup(CleanupStage stage)
{
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(flow dpdk cxx17)
EXPORT_ELEMENT(FlowIPManager_CuckooPP)
EXPORT_ELEMENT(FlowIPManager_CuckooPP-FlowIPManagerIMP)
ELEMENT_MT_SAFE(FlowIPManager_CuckooPP)
