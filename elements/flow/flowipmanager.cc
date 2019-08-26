/*
 * FlowIPManager.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include "flowipmanager.hh"
#include <rte_hash.h>
#include <rte_hash_crc.h>
#include <rte_ethdev.h>

CLICK_DECLS

FlowIPManager::FlowIPManager() : _verbose(1), _flags(0), _timer(this) {
}

FlowIPManager::~FlowIPManager() {
}

int
FlowIPManager::configure(Vector<String> &conf, ErrorHandler *errh)
{

    if (Args(conf, this, errh)
            .read_or_set_p("CAPACITY", _table_size, 65536)
            .read_or_set("RESERVE",_reserve, 0)
			.read_or_set("TIMEOUT", _timeout, 60)
            .complete() < 0)
        return -1;


    return 0;
}



inline uint32_t
ipv4_hash_crc(const void *data,  uint32_t data_len,
                uint32_t init_val)
{
    (void)data_len;
    const IPFlow5ID *k;
    uint32_t t;
    const uint32_t *p;
    k = (const IPFlow5ID *)data;
    t = k->proto();
    p = ((const uint32_t *)k) + 2;
    init_val = rte_hash_crc_4byte(t, init_val);
    init_val = rte_hash_crc_4byte(k->saddr(), init_val);
    init_val = rte_hash_crc_4byte(k->daddr(), init_val);
    init_val = rte_hash_crc_4byte(*p, init_val);
    return init_val;
}

int FlowIPManager::initialize(ErrorHandler *errh) {
    struct rte_hash_parameters hash_params = {0};
    char buf[32];
    hash_params.name = buf;
    hash_params.entries = _table_size;
    hash_params.key_len = sizeof(IPFlow5ID);
    hash_params.hash_func = ipv4_hash_crc;
    hash_params.hash_func_init_val = 0;
    hash_params.extra_flag = _flags;

    _flow_state_size_full = sizeof(FlowControlBlock) + _reserve;

    sprintf(buf, "FlowIPManager");
    hash = rte_hash_create(&hash_params);
    if (!hash)
        return errh->error("Could not init flow table !");

    fcbs =  (FlowControlBlock*)CLICK_ALIGNED_ALLOC(_flow_state_size_full * _table_size);
    CLICK_ASSERT_ALIGNED(fcbs);
    if (!fcbs)
        return errh->error("Could not init data table !");

    if (_timeout > 0) {
	_timer_wheel.initialize(_timeout);
    }

    _timer.schedule_after(Timestamp::make_sec(1));
    return 0;
}

const auto setter = [](FlowControlBlock* prev, FlowControlBlock* next) {
	*(FlowControlBlock**)((void*)&prev->data_32[1]) = next;
	};

void FlowIPManager::run_timer(Timer* t) {
	Timestamp recent = Timestamp::recent_steady();
	_timer_wheel.run_timers([this,recent](FlowControlBlock* prev) -> FlowControlBlock*{
		FlowControlBlock* next = (FlowControlBlock*)((void*)prev->data_32[1]);
		if (prev->lastseen + Timestamp::make_sec(_timeout) < recent + Timestamp::make_sec(1)) {
			//expire
			rte_hash_free_key_with_position(hash, prev->data_32[0]);
		} else {
			_timer_wheel.schedule_after(prev, _timeout - (recent - prev->lastseen).sec(),setter);
		}
		return next;
	});
    t->reschedule_after(Timestamp::make_sec(1));
}

void FlowIPManager::cleanup(CleanupStage stage) {
    if (hash)
        rte_hash_free(hash);
}


void FlowIPManager::process(Packet* p, BatchBuilder& b, const Timestamp& recent) {
    IPFlow5ID fid = IPFlow5ID(p);
    rte_hash*& table = hash;

    int ret = rte_hash_lookup(table, &fid);

    if (ret < 0) { //new flow
        ret = rte_hash_add_key(table, &fid);
        if (ret < 0) {
            click_chatter("Cannot add key (have %d items)!", rte_hash_count(table));
            return;
        }
        fcb_stack = (FlowControlBlock*)((unsigned char*)fcbs + _flow_state_size_full * ret);
        fcb_stack->data_32[0] = ret;
        if (_timeout) {
		if (_flags) {
			_timer_wheel.schedule_after_mp(fcb_stack, _timeout, setter);
		} else {
			_timer_wheel.schedule_after(fcb_stack, _timeout, setter);
		}
        }
    } else {
	fcb_stack = (FlowControlBlock*)((unsigned char*)fcbs + _flow_state_size_full * ret);
    }

    if (b.last == ret) {
        b.append(p);
    } else {
        PacketBatch* batch;
        batch = b.finish();
        if (batch) {
		fcb_stack->lastseen = recent;
            output_push_batch(0, batch);
        }

        b.init();
        b.append(p);
    }
}


void FlowIPManager::push_batch(int, PacketBatch* batch) {
    BatchBuilder b;
    Timestamp recent = Timestamp::recent_steady();
    FOR_EACH_PACKET_SAFE(batch, p) {
        process(p, b, recent);
    }

    batch = b.finish();
    if (batch) {
	fcb_stack->lastseen = recent;
        output_push_batch(0, batch);
    }
}


enum {h_count};
String FlowIPManager::read_handler(Element* e, void* thunk) {
    FlowIPManager* fc = static_cast<FlowIPManager*>(e);

    rte_hash* table = fc->hash;
    switch ((intptr_t)thunk) {
    case h_count:
        return String(rte_hash_count(table));
    default:
        return "<error>";
    }
};

void FlowIPManager::add_handlers() {

}

CLICK_ENDDECLS

EXPORT_ELEMENT(FlowIPManager)
ELEMENT_MT_SAFE(FlowIPManager)
