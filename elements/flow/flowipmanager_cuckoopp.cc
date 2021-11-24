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

    return ret >= 0? flowid : 0;
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


inline void
FlowIPManager_CuckooPP::process(Packet *p, BatchBuilder &b, Timestamp &recent) {
    IPFlow5ID fid = IPFlow5ID(p);
    if (_cache && fid == b.last_id) {
        b.append(p);
        return;
    }

    FlowControlBlock *fcb;
    auto &state = *_tables;

    int ret = find(fid);

    if (ret == 0) {

        uint32_t flowid;
        flowid = state.imp_flows_pop();
        if (unlikely(flowid == 0)) {
            click_chatter("ID is 0 and table is full!");
            p->kill();
            return;
        }

        // INSERT IN TABLE
        ret = insert(fid, flowid);

        if (unlikely(ret == 0)) {
            p->kill();
            return;
        }

        fcb = get_fcb_from_flowid(ret);
        *(get_fcb_flowid(fcb)) = flowid;

        if (_timeout)
            memcpy(get_fcb_key(fcb), &fid, sizeof(IPFlow5ID));

    } // if ret ==0
    else {
        // Old flow
        fcb = get_fcb_from_flowid(ret);
    }

    update_lastseen(fcb, recent);

    if (b.last == ret) {
        b.append(p);
    } else {
        PacketBatch *batch;
        batch = b.finish();
        if (batch) {
#if HAVE_FLOW_DYNAMIC
            fcb_acquire(batch->count())
#endif
            output_push_batch(0, batch);
        }
        fcb_stack = fcb;
        b.init();
        b.append(p);
        b.last = ret;
        if (_cache) {
            b.last_id = fid;
        }
    }
}

void FlowIPManager_CuckooPP::push_batch(int, PacketBatch *batch) {
    BatchBuilder b;
    Timestamp recent = Timestamp::recent_steady();

    FOR_EACH_PACKET_SAFE(batch, p) {
            process(p, b, recent);
    }

    batch = b.finish();
    if (batch) {
#if HAVE_FLOW_DYNAMIC
        fcb_acquire(batch->count())
#endif
        output_push_batch(0, batch);
    }
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(flow dpdk)
EXPORT_ELEMENT(FlowIPManager_CuckooPP)
ELEMENT_MT_SAFE(FlowIPManager_CuckooPP)
