/*
 * flowipmanagerimp_nofid.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include <rte_hash.h>
#include "flowipmanagerimp_nofid.hh"
#include <click/dpdk_glue.hh>
#include <rte_ethdev.h>

CLICK_DECLS

int
FlowIPManagerIMP_NoFID::alloc(int core)
{
    struct rte_hash_parameters hash_params = {0};
    char buf[32];

    hash_params.name = buf;
    hash_params.entries = _table_size;
    hash_params.key_len = sizeof(IPFlow5ID);
    hash_params.hash_func = ipv4_hash_crc;
    hash_params.hash_func_init_val = 0;
    hash_params.extra_flag = _flags;
    VPRINT(1,"DPDK FLAGS ARE %d", _flags);

    VPRINT(1,"[%i] Real capacity for table will be %lu", core, _table_size);


    sprintf(buf, "IMP-%d", core);
    _tables[core].hash = rte_hash_create(&hash_params);

    if(unlikely(_tables[core].hash == nullptr))
    {
	    VPRINT(0,"Could not init flow table %d!", core);
	return 1;
    }

    return 0;
}



uint64_t FlowIPManagerIMP_NoFID::max_key_id(int core) {
    auto& tab = _tables[core];
    rte_hash * ht = reinterpret_cast<rte_hash*>(tab.hash);

    return rte_hash_max_key_id(ht) + 1;
}



int
FlowIPManagerIMP_NoFID::find(IPFlow5ID &f, int core)
{
    auto& tab = _tables[core];
    rte_hash * ht = reinterpret_cast<rte_hash*>(tab.hash);

    int ret = rte_hash_lookup(ht, &f);

    return ret < 0?0 : ret + 1;

}

int
FlowIPManagerIMP_NoFID::insert(IPFlow5ID &f, int, int core)
{
    auto& tab = _tables[core];
    rte_hash * ht = reinterpret_cast<rte_hash *> (tab.hash);

    uint32_t ret = rte_hash_add_key(ht, &f);

    return ret < 0? 0 : ret + 1;
}

int FlowIPManagerIMP_NoFID::get_count(int core) {
    auto& tab = _tables[core];
    rte_hash * ht = reinterpret_cast<rte_hash *> (tab.hash);

    return rte_hash_count(ht);
}


int FlowIPManagerIMP_NoFID::delete_flow(FlowControlBlock * fcb, int core)
{
    int ret = rte_hash_del_key(reinterpret_cast<rte_hash *> (_tables[core].hash), get_fid(fcb));
    // VPRINT(2,"Deletion of entry %p, belonging to flow %i , returned %i: %s", fcb,
		// *get_flowid(fcb), ret, (ret >=0 ? "OK" : ret == -ENOENT ? "ENOENT" : "EINVAL" ));

    return ret;
}

int FlowIPManagerIMP_NoFID::free_pos(int pos, int core)
{
    return rte_hash_free_key_with_position(reinterpret_cast<rte_hash *> (_tables[core].hash), pos);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(flow dpdk dpdk19)
EXPORT_ELEMENT(FlowIPManagerIMP_NoFID)
ELEMENT_MT_SAFE(FlowIPManagerIMP_NoFID)
