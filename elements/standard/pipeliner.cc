// -*- c-basic-offset: 4; related-file-name: "pipeliner.hh" -*-
/*
 * pipeliner.{cc,hh} --
 */

#include <click/config.h>
#include "pipeliner.hh"
#include <click/standard/scheduleinfo.hh>
#include <click/ring.hh>
#include <click/args.hh>
#include <click/error.hh>

CLICK_DECLS


#define PS_MIN_THRESHOLD 2048
//#define PS_BATCH_SIZE 1024

Pipeliner::Pipeliner()
    :   _ring_size(-1),_burst(32),
        _home_thread_id(0), _block(false),
        _active(true),_nouseless(false),_always_up(false),
        _allow_direct_traversal(true), _verbose(true),
        sleepiness(0),_sleep_threshold(0),
        _task(this), _last_start(0)
{
#if HAVE_BATCH
    in_batch_mode = BATCH_MODE_YES;
#endif
}

Pipeliner::~Pipeliner()
{

}

bool
Pipeliner::get_spawning_threads(Bitvector& b, bool, int port) {
    unsigned int thisthread = router()->home_thread_id(this);
    b[thisthread] = 1;
    return false;
}

void Pipeliner::cleanup(CleanupStage) {
    for (unsigned i = 0; i < storage.weight(); i++) {
        Packet* p;
        while ((p = storage.get_value(i).extract()) != 0) {
#if HAVE_BATCH
            if (receives_batch == 1)
                static_cast<PacketBatch*>(p)->kill();
            else
#endif
                p->kill();
        }
    }
}



int
Pipeliner::configure(Vector<String> & conf, ErrorHandler * errh)
{

    if (Args(conf, this, errh)
    .read_p("CAPACITY", _ring_size)
    .read_p("BURST", _burst)
    .read_p("BLOCKING", _block)
    .read("ACTIVE", _active)
    .read("ALWAYS_UP",_always_up)
    .read("DIRECT_TRAVERSAL",_allow_direct_traversal)
    .read("NOUSELESS",_nouseless)
    .read("VERBOSE",_verbose)
    .complete() < 0)
        return -1;

    if (_ring_size <= 0) {
        _ring_size = 1024;
    }

    if (_ring_size < 4) {
        errh->error("Pipeliner ring size must be at least 4");
    }

    if (_burst <= 0) {
        _burst = INT_MAX;
    }

    //Amount of empty run of task after which it unschedule
#if HAVE_BATCH
    _sleep_threshold = _ring_size / 2;
#else
    _sleep_threshold = _ring_size / 4;
#endif

    return 0;
}


int
Pipeliner::initialize(ErrorHandler *errh)
{

    bool fp;
    Bitvector passing = get_passing_threads(false, -1, this, fp);
    storage.compress(passing);
    stats.compress(passing);
    _home_thread_id = home_thread_id();

    for (unsigned i = 0; i < storage.weight(); i++) {
        if (!storage.get_value(i).initialized())
            storage.get_value(i).initialize(_ring_size);
    }

    for (int i = 0; i < passing.size(); i++) {
        if (passing[i] && i != _home_thread_id) {
            click_chatter("%p{element} : Pipeline from %d to %d", this, i, _home_thread_id);
            WritablePacket::pool_transfer(_home_thread_id,i);
        }
    }

    if (_block && !_allow_direct_traversal && passing[_home_thread_id]) {
        return errh->error("Possible deadlock ! Pipeliner is served by thread "
                           "%d, and the same thread can push packets to it. "
                           "As Pipeliner is in blocking mode without direct "
                           "traversal, it could block trying to push a "
                           "packet, preventing the very same thread to drain "
                           "the queue.");
    }

    if (!_nouseless && passing.weight() == 1 && passing[_home_thread_id] == 1) {
        errh->warning("Useless Pipeliner element ! Packets on the input come "
                      "from the same thread that the scheduling thread. If "
                      "this is intended, set NOUSELESS to true but I seriously "
                      "doubt that.");
    }

    ScheduleInfo::initialize_task(this, &_task, _active, errh);

    return 0;
}

#if HAVE_BATCH
void Pipeliner::push_batch(int,PacketBatch* head) {
    if (_allow_direct_traversal && click_current_cpu_id() == (unsigned)_home_thread_id) {
        output(0).push_batch(head);
        return;
    }
    int count = head->count();
    retry:
    if (storage->insert(head)) {
        stats->count += count;
        if (sleepiness >= _sleep_threshold)
            _task.reschedule();
    } else {
        if (_block) {
            if (!_always_up && sleepiness >= _sleep_threshold)
                _task.reschedule();
            stats->dropped++;
            if (_verbose && ((stats->dropped < 10) || ((stats->dropped & 0xffffffff) == 1)))
                click_chatter("%p{element} : congestion", this);
            goto retry;
        }
        int c = head->count();
        head->kill();
        stats->dropped+=c;
        if (_verbose && ((stats->dropped < 10) || ((stats->dropped & 0xffffffff) == 1)))
            click_chatter("%p{element} : Dropped %lu packets : have %u packets in ring", this, stats->dropped, c);
    }
}
#endif

void
Pipeliner::push(int,Packet* p)
{
    if (_allow_direct_traversal && click_current_cpu_id() == (unsigned)_home_thread_id) {
        output(0).push(p);
        return;
    }

retry:
    if (storage->insert(p)) {
        stats->count++;
    } else {
        if (_block) {
            if (!_always_up && sleepiness >= _sleep_threshold)
                _task.reschedule();

            stats->dropped++;
            if (_verbose && (stats->dropped < 10 || ((stats->dropped & 0xffffffff) == 1)))
                click_chatter("%p{element} : congestion", this);

            goto retry;
        }
        p->kill();
        stats->dropped++;
        if (_verbose && (stats->dropped < 10 || ((stats->dropped & 0xffffffff) == 1)))
            click_chatter("%p{element} : Dropped %lu packets : have %u packets in ring", this, stats->dropped, storage->count());
    }

    if (!_always_up && sleepiness >= _sleep_threshold && _active)
        _task.reschedule();
}

#define HINT_THRESHOLD 32
bool
Pipeliner::run_task(Task* t)
{
    bool r = false;
    _last_start++; //Used to RR the balancing of revert storage
    for (unsigned j = 0; j < storage.weight(); j++) {
        int i = (_last_start + j) % storage.weight();
        PacketRing& s = storage.get_value(i);
#if HAVE_BATCH
        PacketBatch* out = NULL;
#endif
        int n = 0;
        while (!s.is_empty() && n++ < _burst) {
#if HAVE_BATCH
            PacketBatch* b = static_cast<PacketBatch*>(s.extract());
            if (unlikely(!receives_batch)) {
                if (out == NULL) {
                    b->set_tail(b);
                    b->set_count(1);
                    out = b;
                } else {
                    out->append_packet(b);
                }
            } else {
                if (out == NULL) {
                    out = b;
                } else {
                    out->append_batch(b);
                }
            }
            //WritablePacket::pool_hint(b->count(),storage.get_mapping(i));
#else
            Packet* p = s.extract();
            output(0).push(p);
            //WritablePacket::pool_hint(HINT_THRESHOLD,storage.get_mapping(i));
            r = true;
#endif
        }

#if HAVE_BATCH
        if (out) {
            output_push_batch(0,out);
            r = true;
        }
#endif

    }
    if (unlikely(!_active))
        return r;

    if (_always_up) {
        t->fast_reschedule();
    } else {
        if (!r) {
            sleepiness++;
            if (sleepiness < _sleep_threshold) {
                t->fast_reschedule();
            }
        } else {
            sleepiness = 0;
            t->fast_reschedule();
        }
    }
    return r;
}

int
Pipeliner::write_handler(const String &conf, Element* e, void*, ErrorHandler* errh)
{
    Pipeliner* p = reinterpret_cast<Pipeliner*>(e);
    if (!BoolArg().parse(conf,p->_active))
        return errh->error("invalid argument");

    if (p->_active)
        p->_task.reschedule();
    else
        p->_task.unschedule();
    return 0;
}

void
Pipeliner::add_handlers()
{
    add_read_handler("dropped", dropped_handler, 0);
    add_read_handler("count", count_handler, 0);
    add_data_handlers("active", Handler::OP_READ, &_active);
    add_write_handler("active", write_handler, 0);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(Pipeliner)
ELEMENT_MT_SAFE(Pipeliner)
