/*
 * flowipmanagermp_timerwheel.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include <rte_hash.h>
#include "flowipmanagermp_timerwheel.hh"
#include <click/dpdk_glue.hh>
#include <rte_ethdev.h>

CLICK_DECLS

int FlowIPManagerMP_TW::parse(Args *args){

    int ret = (*args)
	.read_or_set("LF", _lf, false)
	.consume();

    _reserve += sizeof(FlowControlBlock *); // For the timerwheel
    return ret;

}

FlowIPManagerMP_TW::FlowIPManagerMP_TW():  VirtualFlowIPManagerIMP(), _timer_wheel(TimerWheel<FlowControlBlock>()), _qbsr(0)
{
}

int FlowIPManagerMP_TW::alloc(int core)
{
    assert(core == 0);
    //click_chatter("TW %d -> max timeout %d, deferred %d", core, _timeout * _epochs_per_sec);
    for (int i = 0; i < _timer_wheel.weight(); i ++)
        _timer_wheel.get_value(i).initialize(_timeout * _epochs_per_sec);


    if(_lf) {
	    _flags = RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD
		| RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF
		| RTE_HASH_EXTRA_FLAGS_NO_FREE_ON_DEL;
    } else {
	    _flags = RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD
		| RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY
		| RTE_HASH_EXTRA_FLAGS_NO_FREE_ON_DEL;

    }
    //click_chatter("DEFERRED %d", _have_deferred_free);
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

    return 0;
}

int
FlowIPManagerMP_TW::find(IPFlow5ID &f, int)
{
    const int core = 0;
    auto& tab = _tables[core];
    rte_hash * ht = reinterpret_cast<rte_hash*>(tab.hash);
    uint64_t this_flow=0;

    int ret = rte_hash_lookup_data(ht, &f, (void **)&this_flow);
//    click_chatter("[%d] CLASSED %d %d", click_current_cpu_id(), ret, this_flow);

    return ret>=0 ? this_flow : 0;

}
const auto setter = [](FlowControlBlock* prev, FlowControlBlock* next)
{
    *(FlowIPManagerMP_TW::get_next(prev)) = next;

};

int
FlowIPManagerMP_TW::insert(IPFlow5ID &f, int flowid, int) //dest_core
{
    const int table_core = 0;
    const int dest_core = 0;
    auto& tab = _tables[table_core];
    rte_hash * ht = reinterpret_cast<rte_hash *> (tab.hash);

    uint32_t ret = rte_hash_add_key_data(ht, &f, (void *)(uintptr_t)flowid);


   // assert(click_current_cpu_id() == dest_core);
   // click_chatter("[%d->%d] ret %d insert flow %d -> key %s", click_current_cpu_id(), dest_core, ret, flowid,f.unparse().c_str());
    if (ret == 0) {
        uint32_t this_flow;
        rte_hash_lookup_data(ht, &f, (void **)&this_flow);
        assert(this_flow == flowid);
        if( _timeout > 0 ) {
            _timer_wheel->schedule_after(get_fcb(flowid, dest_core), _timeout * _epochs_per_sec, setter);
       }
    }



    return ret == 0 ? flowid : 0;
}

int FlowIPManagerMP_TW::maintainer(int) {
    const int table_core = 0;
    const int dest_core = 0;
    Timestamp recent = Timestamp::recent_steady();

    _tables[table_core].lock->acquire();
    while (*_qbsr) {
        FlowControlBlock* next = *get_next(*_qbsr);
        flows_push(dest_core, *get_flowid(*_qbsr));
        *_qbsr = next;
    }
    _tables[table_core].lock->release();
    //click_chatter("Clean core %d for core %d, tw %p", click_current_cpu_id(), dest_core ,&*_timer_wheel );
    int checker = 0;
    _timer_wheel->run_timers([this,recent,&checker, dest_core, table_core](FlowControlBlock* prev) -> FlowControlBlock*{
        if(unlikely(checker >= _table_size))
        {
            click_chatter("Loop detected!");
            abort();
        }
        FlowControlBlock * next = *get_next(prev);
        //Verify lastseen is not in the future
        if (unlikely(recent <= prev->lastseen)) {

            int64_t old = (recent - prev->lastseen).msecval();
            click_chatter("Old %li : %s %s fid %d",old, recent.unparse().c_str(), prev->lastseen.unparse().c_str(),get_flowid(prev) );

            _timer_wheel->schedule_after(prev, _timeout * _epochs_per_sec, setter);
            return next;
        }

        int old = (recent - prev->lastseen).msecval();

        if (old + _recycle_interval_ms >= _timeout * 1000) {
            //click_chatter("[%d] Release %p (%i)->%s as it is expired since %d", click_current_cpu_id(), prev, *get_flowid(prev),get_fid(prev)->unparse().c_str(), old);
            //expire

            int pos = rte_hash_del_key(reinterpret_cast<rte_hash *> (_tables[table_core].hash), get_fid(prev));
            if (likely(pos>=0))
            {
                *get_next(prev) = *_qbsr;
                *_qbsr = prev;
                if (_have_deferred_free)
                    rte_hash_free_key_with_position(reinterpret_cast<rte_hash *> (_tables[table_core].hash), pos);
            }
            else
            {
                click_chatter("[%d->%d] error %d Removed a key not in the table flow %d (%s)...", click_current_cpu_id(), dest_core , pos , *get_flowid(prev), get_fid(prev)->unparse().c_str());
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
    //click_chatter("%s finished to check expirations after %i seconds", __FUNCTION__,
	 //    Timestamp::recent_steady().sec()  - recent.sec());

    return checker;
}


int FlowIPManagerMP_TW::delete_flow(FlowControlBlock * fcb, int)
{
    abort();
    int ret = rte_hash_del_key(reinterpret_cast<rte_hash *> (_tables[0].hash), get_fid(fcb));
    //if(likely(ret>=0))
	//rte_hash_free_key_with_position(reinterpret_cast<rte_hash *> (_tables[0].hash), ret);
    VPRINT(2,"Deletion of entry %p, belonging to flow %i , returned %i: %s", fcb,
		*get_flowid(fcb), ret, (ret >=0 ? "OK" : ret == -ENOENT ? "ENOENT" : "EINVAL" ));

    return ret;
}

int FlowIPManagerMP_TW::free_pos(int pos, int)
{
    abort();
    return rte_hash_free_key_with_position(reinterpret_cast<rte_hash *> (_tables[0].hash), pos);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(flow dpdk dpdk19)
EXPORT_ELEMENT(FlowIPManagerMP_TW)
