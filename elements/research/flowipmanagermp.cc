#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include <rte_hash.h>
#include "flowipmanagermp.hh"
#include <click/dpdk_glue.hh>
#include <rte_ethdev.h>

CLICK_DECLS


int
FlowIPManagerMP::parse(Args *args) {
    return (*args)
	.read_or_set("LF", _lf, false)
	    .consume();
}

int
FlowIPManagerMP::alloc(int core)
{
    if (_tables[0].hash == 0)
    {
        if(_lf)
            _flags = RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD
            | RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF;
        else
            _flags = RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD
            | RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY;
        _flags |= RTE_HASH_EXTRA_FLAGS_NO_FREE_ON_DEL;

        struct rte_hash_parameters hash_params = {0};
        char buf[32];

        hash_params.name = buf;
        hash_params.entries = _table_size;
        hash_params.key_len = sizeof(IPFlow5ID);
        hash_params.hash_func = ipv4_hash_crc;
        hash_params.hash_func_init_val = 0;
        hash_params.extra_flag = _flags;

        VPRINT(1,"[%i] Real capacity for table will be %lu", core,  _table_size);

        sprintf(buf, "MP-%i", core);
        _tables[0].hash = rte_hash_create(&hash_params);

        if(unlikely(_tables[0].hash == nullptr))
        {
            VPRINT(0,"Could not init flow table %d!", 0);
            return 1;
        }
    }
    else
	VPRINT(1,"alloc on thread %i: ignore");
    return 0;
}



int
FlowIPManagerMP::find(IPFlow5ID &f, int core)
{
    core = 0;

    auto& tab = _tables[core];
    rte_hash * ht = reinterpret_cast<rte_hash*>(tab.hash);
    uint64_t this_flow=0;

    int ret = rte_hash_lookup_data(ht, &f, (void **)&this_flow);
    //click_chatter("FIND: %u -> %i -> %c", hash, ret, (ret>0) ?'Y' : 'N');
    return ret>=0 ? this_flow : 0;

}

int
FlowIPManagerMP::insert(IPFlow5ID &f, int flowid, int core)
{
    core = 0;

    auto& tab = _tables[core];
    rte_hash * ht = reinterpret_cast<rte_hash *> (tab.hash);

    uint64_t ff = flowid;
    uint32_t ret = rte_hash_add_key_data(ht, &f, (void *) ff);

    //TODO: What is the correct return value?
    return ret == 0 ? flowid : 0;
}


int FlowIPManagerMP::delete_flow(FlowControlBlock * fcb, int core)
{
    core = 0;
    int ret = rte_hash_del_key(reinterpret_cast<rte_hash *> (_tables[core].hash), get_fid(fcb));
	VPRINT(3,"Deletion of entry %p, belonging to flow %i , returned %i: %s", fcb,
		*get_flowid(fcb), ret, (ret >=0 ? "OK" : ret == -ENOENT ? "ENOENT" : "EINVAL" ));

    return ret;
}

int FlowIPManagerMP::free_pos(int pos, int core)
{
    core = 0;
    return rte_hash_free_key_with_position(reinterpret_cast<rte_hash *> (_tables[core].hash), pos);
}


CLICK_ENDDECLS

ELEMENT_REQUIRES(flow dpdk dpdk19)
EXPORT_ELEMENT(FlowIPManagerMP)
ELEMENT_MT_SAFE(FlowIPManagerMP)
