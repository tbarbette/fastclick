// -*- c-basic-offset: 4 -*-
#ifndef CLICK_PIPELINER_HH
#define CLICK_PIPELINER_HH

#include <click/batchelement.hh>
#include <click/task.hh>
#include <click/ring.hh>
#include <click/multithread.hh>

CLICK_DECLS

/*
=c

Pipeliner

=s storage

=d

Fast version of ThreadSafeQueue->Unqueue, allowing to offload processing
of packets pushed to this element to another one, without the inherent
scheduling cost of normal queues. Multiple thread can push packets to
this queue, and the home thread of this element will push packet out.


*/



class Pipeliner: public BatchElement {



public:

    Pipeliner();
    ~Pipeliner();

    const char *class_name() const      { return "Pipeliner"; }
    const char *port_count() const      { return "1-/1"; }
    const char *processing() const      { return PUSH; }

    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;

    int initialize(ErrorHandler*) CLICK_COLD;

    void cleanup(CleanupStage);

    bool blocking() const {
        return _block;
    }

    bool get_spawning_threads(Bitvector& b, bool isoutput, int port) override;

#if HAVE_BATCH
    void push_batch(int,PacketBatch*);
#endif
    void push(int,Packet*);

    bool run_task(Task *);

    unsigned long n_dropped() {
        PER_THREAD_MEMBER_SUM(unsigned long,total,stats,dropped);
        return total;
    }

    unsigned long n_count() {
        PER_THREAD_MEMBER_SUM(unsigned long,total,stats,count);
        return total;
    }

    static String dropped_handler(Element *e, void *)
    {
        Pipeliner *p = static_cast<Pipeliner *>(e);
        return String(p->n_dropped());
    }

    static String count_handler(Element *e, void *)
    {
        Pipeliner *p = static_cast<Pipeliner *>(e);
        return String(p->n_count());
    }

    static int write_handler(const String &conf, Element* e, void*, ErrorHandler*);
    void add_handlers() CLICK_COLD;

    int _ring_size;
    int _burst;
    int _home_thread_id;
    bool _block;
    bool _active;
    bool _nouseless;
    bool _always_up;
    bool _allow_direct_traversal;
    bool _verbose;
    typedef DynamicRing<Packet*> PacketRing;

    per_thread_oread<PacketRing> storage;
    struct stats {
        stats() : dropped(0), count(0) {

        }
        unsigned long dropped;
        unsigned long count;
    };
    per_thread_oread<struct stats> stats;
    volatile int sleepiness;
    int _sleep_threshold;

  protected:
    Task _task;
    unsigned int _last_start;


};

CLICK_ENDDECLS
#endif
