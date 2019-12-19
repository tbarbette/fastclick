/*
 * FlowIPManagerHMP.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include "flowipmanagerhmp.hh"

CLICK_DECLS

FlowIPManagerHMP::FlowIPManagerHMP() {
	_current = 0;
}

FlowIPManagerHMP::~FlowIPManagerHMP() {

}

int
FlowIPManagerHMP::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
		.read_or_set_p("CAPACITY", _table_size, 65536)
            .read_or_set("RESERVE", _reserve, 0)
            .read_or_set("VERBOSE", _verbose, 0)
            .complete() < 0)
        return -1;

    find_children(_verbose);

    router()->get_root_init_future()->postOnce(&_fcb_builded_init_future);
    _fcb_builded_init_future.post(this);

    return 0;
}

int FlowIPManagerHMP::initialize(ErrorHandler *errh) {
    return 0;
}

int FlowIPManagerHMP::solve_initialize(ErrorHandler *errh) {
	_flow_state_size_full = sizeof(FlowControlBlock) + _reserve;

	_hash.resize_clear(_table_size);

	fcbs =  (FlowControlBlock*)CLICK_ALIGNED_ALLOC(_flow_state_size_full * _table_size);
	CLICK_ASSERT_ALIGNED(fcbs);
	if (!fcbs)
		return errh->error("Could not init data table !");

    return Router::InitFuture::solve_initialize(errh);
}

void FlowIPManagerHMP::cleanup(CleanupStage stage) {

}


void FlowIPManagerHMP::pre_migrate(DPDKDevice* dev, int from, Vector<Pair<int,int>> gids) {

}


void FlowIPManagerHMP::post_migrate(DPDKDevice* dev, int from) {

}

void FlowIPManagerHMP::process(Packet* p, BatchBuilder& b) {
	IPFlow5ID fid = IPFlow5ID(p);
	bool first = false;

	auto ptr = _hash.find_create(fid, [this,&first](){
		int id = _current.fetch_and_add(1);first= true;return id;
		//click_chatter("Creating id %d",id);
	});

	int ret = *ptr;


	if (b.last == ret) {
		b.append(p);
	} else {
		PacketBatch* batch;
		batch = b.finish();
		if (batch)
			output_push_batch(0, batch);
		fcb_stack = (FlowControlBlock*)((unsigned char*)fcbs + _flow_state_size_full * ret);
		b.init();
        b.append(p);
	}
}


void FlowIPManagerHMP::init_assignment(Vector<unsigned> table) {

}



void FlowIPManagerHMP::push_batch(int, PacketBatch* batch) {
	BatchBuilder b;

	FOR_EACH_PACKET_SAFE(batch, p) {
		process(p, b);
	}
	assert(fcb_stack);
	batch = b.finish();
	if (batch)
		output_push_batch(0, batch);

}





CLICK_ENDDECLS

EXPORT_ELEMENT(FlowIPManagerHMP)
ELEMENT_MT_SAFE(FlowIPManagerHMP)
