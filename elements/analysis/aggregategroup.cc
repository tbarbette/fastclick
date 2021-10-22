/*
 * aggregategroup.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/packet.hh>
#include <click/packet_anno.hh>
#include "aggregategroup.hh"

#include <click/master.hh>


CLICK_DECLS

AggregateGroup::AggregateGroup() : timeout(0) {
	in_batch_mode = BATCH_MODE_NEEDED;
}


int
AggregateGroup::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
    .read_p("TIMER", timeout)
	.complete() < 0)
	return -1;

	if (timeout > 0) {

		for (int i = 0; i < i; i++) {
			State &s = _state.get_value_for_thread(i);
			Task* task = new Task(this);
			task->initialize(this,false);
			task->move_thread(i);
			s.timers = new Timer(task);
			s.timers->initialize(this);
		}
	}

	return 0;
}

bool AggregateGroup::run_task(Task *task) {
	State &s = _state.get();

	click_chatter("Timeout");
	if (s.last_packet) {
		click_chatter("But no packet...");
		return true;
	}

	PacketBatch* p = s.last_packet;
	output_push_batch(0,p);
	s.last_packet = NULL;
	return true;
}

void AggregateGroup::push_batch(int, PacketBatch *p) {
	State &s = _state.get();

	if (s.last_packet == NULL) {
		click_chatter("Received packet and launching timer");
		s.last_packet = p;
		if (timeout > 0) {
			s.timers->schedule_after(Timestamp::make_usec(timeout));
		}
		return;
	}

	if (AGGREGATE_ANNO(s.last_packet->first()) == AGGREGATE_ANNO(p->first())) {
		PacketBatch * parent =s.last_packet;
		parent->append_batch(p);
		if (timeout > 0)
			s.timers->reschedule_after(Timestamp::make_usec(timeout));
		//output(0).push(last_packet[tid]);
	} else {
		if (timeout > 0)
			s.timers->unschedule();

		output_push_batch(0,s.last_packet);
		s.last_packet = 0;
		output_push_batch(0,p);
	}
}

CLICK_ENDDECLS
EXPORT_ELEMENT(AggregateGroup)
ELEMENT_REQUIRES(batch)
