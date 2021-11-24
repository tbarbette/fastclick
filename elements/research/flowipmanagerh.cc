#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include <rte_hash.h>
#include "flowipmanagerh.hh"
#include <click/dpdk_glue.hh>
#include <rte_ethdev.h>

CLICK_DECLS


int
FlowIPManagerH::alloc(int i)
{
    if (_tables[0].hash == 0)
    {
        auto t = new HashTableH<IPFlow5ID,int>();
    	_tables[0].hash = t;
        t->resize_clear(_table_size);
    	t->disable_mt();
    	VPRINT(1,"Created H table by thread %i", i);
    }
    else
	VPRINT(1,"alloc on thread %i: ignore", i);
    return 0;
}



int
FlowIPManagerH::find(IPFlow5ID &f, int core)
{
    HashTableH<IPFlow5ID, int> * ht = reinterpret_cast<HashTableH<IPFlow5ID, int> *> (_tables[0].hash);

    uint64_t this_flow=0;
    auto ret = ht->find(f);
    return ret!=0 ? (int) *ret : 0;

}

int
FlowIPManagerH::insert(IPFlow5ID &f, int flowid, int core)
{

    HashTableH<IPFlow5ID, int> * ht = reinterpret_cast<HashTableH <IPFlow5ID, int> *> (_tables[0].hash);

    ht->find_insert(f, flowid);
    return flowid;
}


int FlowIPManagerH::delete_flow(FlowControlBlock * fcb, int core)
{
    reinterpret_cast<HashTableH <IPFlow5ID, int> *> (_tables[0].hash)->erase(*get_fid(fcb));
    return 1;
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowIPManagerH)
ELEMENT_MT_SAFE(FlowIPManagerH)
ELEMENT_REQUIRES(dpdk)
