/*
 * flowipmanagerimp_timerwheel.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include <rte_hash.h>
#include "flowipmanagerimp_timerwheel.hh"
#include <click/dpdk_glue.hh>
#include <rte_ethdev.h>

CLICK_DECLS


int FlowIPManagerIMP_TW::parse(Args *args){

    int ret = (*args)
	.consume();

    _reserve += sizeof(FlowControlBlock *); // For the timerwheel
    return ret;

}

int
FlowIPManagerIMP_TW::alloc(int core)
{
    struct rte_hash_parameters hash_params = {0};
    char buf[32];

    hash_params.name = buf;
    hash_params.entries = _table_size;
    hash_params.key_len = sizeof(IPFlow5ID);
    hash_params.hash_func = ipv4_hash_crc;
    hash_params.hash_func_init_val = 0;
    hash_params.extra_flag = _flags;
    VPRINT(0,"DPDK FLAGS ARE %d", _flags);

    VPRINT(1,"[%i] Real capacity for table will be %lu", core, _table_size);


    sprintf(buf, "IMP_TW-%d", core);
    _tables[core].hash = rte_hash_create(&hash_params);

    if(unlikely(_tables[core].hash == nullptr))
    {
	    VPRINT(0,"Could not init flow table %d!", core);
	    return 1;
    }

   _timer_wheel.get_value_for_thread(core).initialize(_timeout * _epochs_per_sec);

    return 0;
}

int
FlowIPManagerIMP_TW::find(IPFlow5ID &f, int core)
{
    auto& tab = _tables[core];
    rte_hash * ht = reinterpret_cast<rte_hash*>(tab.hash);
    uint64_t this_flow=0;

    int ret = rte_hash_lookup_data(ht, &f, (void **)&this_flow);

    return ret>=0 ? this_flow : 0;

}
const auto setter = [](FlowControlBlock* prev, FlowControlBlock* next)
{
    *(FlowIPManagerIMP_TW::get_next(prev)) = next;

};

int
FlowIPManagerIMP_TW::insert(IPFlow5ID &f, int flowid, int core)
{
    auto& tab = _tables[core];
    rte_hash * ht = reinterpret_cast<rte_hash *> (tab.hash);

    uint64_t ff = flowid;
    uint32_t ret = rte_hash_add_key_data(ht, &f, (void *) ff);
   
    if(ret == 0 && _timeout > 0 ) {
	_timer_wheel->schedule_after(get_fcb(flowid, core), _timeout * _epochs_per_sec, setter);

    }
    
    //TODO: What is the correct return value?
    return ret == 0 ? flowid : 0;
}

int FlowIPManagerIMP_TW::maintainer(int core) {
    Timestamp recent = Timestamp::recent_steady();
    //std::stack<int> freed_pos;
    int checker = 0;
    _timer_wheel->run_timers([this,recent,&checker, core](FlowControlBlock* prev) -> FlowControlBlock*{
        if(unlikely(checker >= _table_size))
        {
            click_chatter("Loop detected!");
            abort();
        }
        FlowControlBlock * next = *get_next(prev);
        if (unlikely(recent <= prev->lastseen)) {

            //int64_t old = (recent - prev->lastseen).msecval();
            //click_chatter("Old %li : %s %s fid %d",old, recent.unparse().c_str(), prev->lastseen.unparse().c_str(),get_flowid(prev) );

            _timer_wheel->schedule_after(prev, _timeout * _epochs_per_sec, setter);
            return next;
        }

        int old = (recent - prev->lastseen).msecval();

        if (old + _recycle_interval_ms >= _timeout * 1000) {
            //click_chatter("[%d] Release %p (%i)->%s as it is expired since %d", click_current_cpu_id(), prev, *get_flowid(prev),get_fid(prev)->unparse().c_str(), old);
            //expire


            int pos = rte_hash_del_key(reinterpret_cast<rte_hash *> (_tables[core].hash), get_fid(prev));
            if (likely(pos>=0))
            {
                flows_push(core, *get_flowid(prev));
            }
            else
            {
                click_chatter("[%d] error %d Removed a key not in the table flow %d (%s)...", click_current_cpu_id(), pos , *get_flowid(prev), get_fid(prev)->unparse().c_str());
            }

            checker++;
        } else {
            //click_chatter("Cascade %p, time %d, fid %i next is %p?", prev, old, *get_flowid(prev), *next );
            //No need for lock as we'll be the only one to enqueue there
            if (likely(prev != *get_next(prev))) {
                int r = (_timeout * 1000) - old; //Time left in ms
                r = (r * (_epochs_per_sec)) / 1000;
                assert(r > 0);
                assert(r <= _timeout * _epochs_per_sec);
                _timer_wheel->schedule_after(prev, r, setter);
            }
            else
            {
                click_chatter("Looping on the same entry. do not schedule!");
                abort();
            }
        }
        return next;
    });
    // click_chatter("%s finished to check expirations after %i seconds", __FUNCTION__,
	    // Timestamp::recent_steady().sec()  - recent.sec());

    return checker;
}


int FlowIPManagerIMP_TW::delete_flow(FlowControlBlock * fcb, int core)
{
    int ret = rte_hash_del_key(reinterpret_cast<rte_hash *> (_tables[core].hash), get_fid(fcb));
    //if(likely(ret>=0))
	//rte_hash_free_key_with_position(reinterpret_cast<rte_hash *> (_tables[core].hash), ret);
    VPRINT(2,"Deletion of entry %p, belonging to flow %i , returned %i: %s", fcb, 
		*get_flowid(fcb), ret, (ret >=0 ? "OK" : ret == -ENOENT ? "ENOENT" : "EINVAL" ));

    return ret;
}

int FlowIPManagerIMP_TW::free_pos(int pos, int core)
{
    return rte_hash_free_key_with_position(reinterpret_cast<rte_hash *> (_tables[core].hash), pos);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(flow dpdk dpdk19)
EXPORT_ELEMENT(FlowIPManagerIMP_TW)
ELEMENT_MT_SAFE(FlowIPManagerIMP_TW)
