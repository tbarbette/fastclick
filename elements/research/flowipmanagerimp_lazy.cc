#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include <rte_hash.h>
#include "flowipmanagerimp_lazy.hh"
#include <click/dpdk_glue.hh>
#include <rte_ethdev.h>
#include <click/standard/scheduleinfo.hh>

CLICK_DECLS

int FlowIPManagerIMP_lazy::parse(Args *args){

    return (*args)
	.read_or_set("ALWAYS_RECYCLE", _always_recycle, 1)
	.consume();
}

int
FlowIPManagerIMP_lazy::alloc(int core)
{
    struct rte_hash_parameters hash_params = {0};
    char buf[32];

    hash_params.name = buf;
    hash_params.entries = _table_size;
    hash_params.key_len = sizeof(IPFlow5ID);
    hash_params.hash_func = ipv4_hash_crc;
    hash_params.hash_func_init_val = 0;
    if(_always_recycle)
	_flags = _flags | RTE_HASH_EXTRA_FLAGS_ALWAYS_RECYCLE;
    hash_params.extra_flag = _flags;
    hash_params.lifetime  = _timeout_ms / _recycle_interval_ms;

    VPRINT(1,"[%i] Real capacity for table will be %lu", core,  _table_size);
    VPRINT(1,"%s: HT lazy timeout is set to %i, while active timeout is set to %i",class_name(), _timeout, _timeout);


    sprintf(buf, "LAZY-%d",core);
    _tables[core].hash = rte_hash_create(&hash_params);

    if(unlikely(_tables[core].hash == nullptr))
    {
	    VPRINT(0,"Could not init flow table %d!", core);
	    return 1;
    }

#if IMP_COUNTERS
    if(_lazytables == 0)
    {
	  VPRINT(1,"Initialized lazy counters table");
	  _lazytables = CLICK_ALIGNED_NEW(lazytable, _tables_count);
	  CLICK_ASSERT_ALIGNED(_lazytables);
    }

#endif

    if(!_timer)
	_timer = new Timer(this);

    if(_timer && !_timer->initialized())
    {
        _timer->initialize(this, true);
        VPRINT(1,"Updating recycle epoch every %d ms", _recycle_interval_ms);
        _timer->schedule_after_msec(_recycle_interval_ms);
    }
    return 0;
}



int
FlowIPManagerIMP_lazy::find(IPFlow5ID &f, int core)
{
    auto& tab = _tables[core];
    rte_hash * ht = reinterpret_cast<rte_hash*>(tab.hash);
    uint64_t this_flow=0;

    //_recent is updated by timer

    //int ret = rte_hash_lookup_data(ht, &f, (void **)&this_flow);
    int ret = rte_hash_lookup_data_t(ht, &f, (void **)&this_flow, _recent);

    return ret>=0 ? this_flow : 0;
}

int
FlowIPManagerIMP_lazy::insert(IPFlow5ID &f, int flowid, int core)
{
    auto& tab = _tables[core];
    rte_hash * ht = reinterpret_cast<rte_hash *> (tab.hash);

    int64_t old_flow;
    IPFlow5ID old_key;
    int64_t ret;
    old_flow = flowid;

    // We assume that _recent would have been refreshed during the find
    // So it is already "recent"
    ret = rte_hash_add_key_data_recycle_keepdata_t(ht, &f, (void *) flowid, (void *) &old_key, (void *) &old_flow, _recent);

    //TODO: Handle entry at 0
    if(unlikely(_verbose))
	    click_chatter("HT insert returned %i for flowid %i. old_flow is %i.", ret, flowid, old_flow);

    if(ret >= 0)
    {
#if IMP_COUNTERS
	    _lazytables[core].total_flows++;
#endif
	if( flowid != old_flow)
	    {
#if IMP_COUNTERS
	        _lazytables[core].recycled_entries++;
#endif
           flows_push(core, old_flow);
	}
    }
    else
	flowid = 0;


    return flowid;

    // TODO: What is the correct return value?
}


int FlowIPManagerIMP_lazy::delete_flow(FlowControlBlock * fcb, int core)
{
    int ret = rte_hash_del_key(reinterpret_cast<rte_hash *> (_tables[core].hash), get_fid(fcb));
    //if(likely(ret>=0))
	//rte_hash_free_key_with_position(reinterpret_cast<rte_hash *> (_tables[core].hash), ret);
    if(unlikely(_verbose))
	click_chatter("Deletion of entry %p, belonging to flow %i , returned %i: %s", fcb,
		*get_flowid(fcb), ret, (ret >=0 ? "OK" : ret == -ENOENT ? "ENOENT" : "EINVAL" ));

    return ret;
}

int FlowIPManagerIMP_lazy::free_pos(int pos, int core)
{
    return rte_hash_free_key_with_position(reinterpret_cast<rte_hash *> (_tables[core].hash), pos);
}

int FlowIPManagerIMP_lazy::maintainer(int core)
{
    // In lazy deletion, the mantainer does nothing!
    //click_chatter("Mantainer on Lazy deletion HT: doing nothing!");
    return 0;
}

enum{
#if IMP_COUNTERS
    h_recycled_entries=2000, h_total_flows
#endif
};


String FlowIPManagerIMP_lazy::read_handler(Element *e, void *thunk) {
  FlowIPManagerIMP_lazy *f = static_cast<FlowIPManagerIMP_lazy*>(e);

  switch ((intptr_t)thunk) {
#if IMP_COUNTERS
  case h_recycled_entries:
  case h_total_flows:
    return String(f->get_lazy_counter((intptr_t)thunk));
#endif
  default:
    return VirtualFlowIPManagerIMP::read_handler(e, thunk);
  }
}

void FlowIPManagerIMP_lazy::add_handlers() {
    VirtualFlowIPManagerIMP::add_handlers();
#if IMP_COUNTERS
    add_read_handler("recycled_entries", read_handler, h_recycled_entries);
    add_read_handler("total_flows", read_handler, h_total_flows);
#endif
}

String FlowIPManagerIMP_lazy::get_lazy_counter(int cnt)
{
    uint64_t s = 0;
    for(int i=0; i< _tables_count; i++)
	    switch(cnt){
#if IMP_COUNTERS
		case h_recycled_entries:
		    s += _lazytables[i].recycled_entries;
		    break;
		case h_total_flows:
		    s += _lazytables[i].total_flows;
		    break;
#endif
	    }
	   return String(s);
}


void FlowIPManagerIMP_lazy::run_timer(Timer *t)
{
    _recent = click_jiffies() /  _recycle_interval_ms;

    _timer->schedule_after_msec(_recycle_interval_ms);
}


CLICK_ENDDECLS

//TODO: fix dependecies!
ELEMENT_REQUIRES(flow dpdk dpdk19 dpdk_lazyht)
EXPORT_ELEMENT(FlowIPManagerIMP_lazy)
ELEMENT_MT_SAFE(FlowIPManagerIMP_lazy)
