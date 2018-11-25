/*
 * minbatch.{cc,hh}
 */
#include <click/config.h>
#include "minbatch.hh"
#include <click/glue.hh>
#include <click/args.hh>
#include <click/packet.hh>
#include <click/packet_anno.hh>
#include <click/master.hh>


CLICK_DECLS

MinBatch::MinBatch() : _burst(32),_timeout(-1) {
    in_batch_mode = BATCH_MODE_NEEDED;
}


int
MinBatch::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read_p("BURST", _burst)
        .read_p("TIMER", _timeout)
        .complete() < 0)
        return -1;

    if (_timeout >= 0) {
        for (unsigned i = 0; i < click_max_cpu_ids(); i++) {
            State &s = _state.get_value_for_thread(i);
            Task* task = new Task(this);
            task->initialize(this,false);
            task->move_thread(i);
            s.timers = new Timer(task);
            s.timers->initialize(this);
            s.timers->move_thread(i);
        }
    }

    return 0;
}

bool MinBatch::run_task(Task *task) {
    State &s = _state.get();

    if (s.last_batch) {
        return true;
    }

    PacketBatch* p = s.last_batch;
    output_push_batch(0,p);
    s.last_batch = nullptr;
    return true;
}

void MinBatch::push_batch(int port, PacketBatch *p) {
    State &s = _state.get();

    if (s.last_batch == nullptr) {
        s.last_batch = p;

    } else {
        s.last_batch->append_batch(p);
    }

    if (s.last_batch->count() < _burst) {
        if (_timeout >= 0) {
            s.timers->schedule_after(Timestamp::make_usec(_timeout));
        }
    } else {
        if (_timeout >= 0)
            s.timers->unschedule();

        p = s.last_batch;
        s.last_batch = nullptr;
        output_push_batch(0,p);
    }
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(batch)
EXPORT_ELEMENT(MinBatch)
