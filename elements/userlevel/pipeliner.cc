// -*- c-basic-offset: 4; related-file-name: "pipeliner.hh" -*-
/*
 * pipeliner.{cc,hh} --
 */

#include <click/config.h>
#include "pipeliner.hh"
#include <click/standard/scheduleinfo.hh>
#include <click/ring.hh>

CLICK_DECLS


#define PS_MIN_THRESHOLD 2048
//#define PS_BATCH_SIZE 1024

Pipeliner::Pipeliner()
    :   sleepiness(0),out_id(0),_task(NULL),last_start(0) {
}

Pipeliner::~Pipeliner()
{

}


bool
Pipeliner::get_runnable_threads(Bitvector& b) {
    unsigned int thisthread = router()->home_thread_id(this);
    b.clear();
    b[thisthread] = 1;
    return false;
}


void Pipeliner::cleanup(CleanupStage) {
    for (unsigned i = 0; i < storage.size(); i++) {
#if HAVE_BATCH
        PacketBatch* p;
#else
        Packet* p;
#endif
        while ((p = storage.get_value(i).extract()) != 0) {
            p->kill();
        }
    }
}



int
Pipeliner::configure(Vector<String> &, ErrorHandler *)
{

/*
    if (Args(conf, this, errh)
    .read("MAXTHREADS", maxthreads)
    .read("THREADOFFSET", threadoffset)

    .complete() < 0)
        return -1;
*/
    return 0;
}


int
Pipeliner::initialize(ErrorHandler *errh)
{

    Bitvector v = get_threads();
    storage.compress(v);
    stats.compress(v);
    out_id = router()->home_thread_id(this);


    for (int i = 0; i < v.size(); i++) {
        if (v[i]) {
            click_chatter("Pipeline from %d to %d",i,out_id);
            WritablePacket::pool_transfer(out_id,i);
        }
    }

    _task = new Task(this);
    ScheduleInfo::initialize_task(this, _task, true, errh);
    _task->move_thread(out_id);

    return 0;
}

#if HAVE_BATCH
#define SLEEPINESS_THRESHOLD 4
void Pipeliner::push_batch(int,PacketBatch* head) {
    if (storage->insert(head)) {
        if (sleepiness >= SLEEPINESS_THRESHOLD)
                    _task->reschedule();
    } else {
        //click_chatter("Drop!");
        head->kill();
    }
}
#else
#define SLEEPINESS_THRESHOLD 128
void Pipeliner::push(int,Packet* p) {
    if (storage->insert(p)) {

    } else {
        p->kill();
        stats->dropped++;
		if (stats->dropped % 100 == 1)
			click_chatter("%s : Dropped %d packets : have %d packets", name().c_str(), stats->dropped, storage->count());
    }
    if (sleepiness >= SLEEPINESS_THRESHOLD)
        _task->reschedule();
}

#endif
#define HINT_THRESHOLD 32
bool
Pipeliner::run_task(Task* t)
{
    bool r = false;
    last_start++; //Used to RR the balancing of revert storage
    for (unsigned j = 0; j < storage.size(); j++) {
        int i = (last_start + j) % storage.size();
        PacketBatch* out = NULL;
        uint32_t head = storage.get_value(i).head;
        uint32_t tail = storage.get_value(i).tail;
        while (head != tail) {
#if HAVE_BATCH
            PacketBatch* b = storage.get_value(i).ring[tail % PIPELINE_RING_SIZE];
            //pool_hint(b->count(),storage.get_mapping(i));
            if (out == NULL) {
                out = b;
            } else {
                out->append_batch(b);
            }
            //click_chatter("Read %d[%d]",storage.get_mapping(i),tail % PIPELINE_RING_SIZE);
#else
            (void)out;
            //click_chatter("Read %d[%d]",storage.get_mapping(i),tail % PIPELINE_RING_SIZE);
            Packet* p = storage.get_value(i).ring[tail % PIPELINE_RING_SIZE];
            output(0).push(p);

            if (unlikely(tail % HINT_THRESHOLD == 0)) {
                //WritablePacket::pool_hint(HINT_THRESHOLD,storage.get_mapping(i));
            }

            r = true;
#endif

            tail++;
        }

#if HAVE_BATCH
        if (out) {
            storage.get_value(i).tail = tail;
            output(0).push_batch(out);
            r = true;
        }
#else
        storage.get_value(i).tail = tail;
#endif

    }
    if (!r) {
        sleepiness++;
        if (sleepiness < SLEEPINESS_THRESHOLD) {
            t->fast_reschedule();
        }
    } else {
        sleepiness = 0;
        t->fast_reschedule();
    }
    return r;

}

void Pipeliner::add_handlers()
{
    add_read_handler("n_dropped", dropped_handler, 0);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(Pipeliner)
ELEMENT_MT_SAFE(Pipeliner)
