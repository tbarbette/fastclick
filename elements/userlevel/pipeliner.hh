// -*- c-basic-offset: 4 -*-
#ifndef CLICK_PIPELINER_HH
#define CLICK_PIPELINER_HH

#include <click/batchelement.hh>
#include <click/task.hh>
#include <click/ring.hh>
#include <click/multithread.hh>
#include <vector>

CLICK_DECLS

/*
=c

Pipeliner

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


    bool get_runnable_threads(Bitvector& b);

#if HAVE_BATCH
    void push_batch(int,PacketBatch*);
#endif
    void push_packet(int,Packet*);

    bool run_task(Task *);

    unsigned long int n_dropped() {
        unsigned long int total = 0;
        for (unsigned i = 0; i < stats.size(); i++)
            total += stats.get_value(i).dropped;
        return total;
    }

    static String dropped_handler(Element *e, void *)
    {
        Pipeliner *p = static_cast<Pipeliner *>(e);
        return String(p->n_dropped());
    }
    void add_handlers() CLICK_COLD;

    int _ring_size;
    bool _block;

    typedef DynamicRing<Packet*> PacketRing;

    per_thread_compressed<PacketRing> storage;
    struct stats {
        stats() : dropped(0) {

        }
        unsigned long int dropped;
    };
    per_thread_compressed<struct stats> stats;
    int out_id;
    volatile int sleepiness;

  protected:
    Task* _task;
    unsigned int last_start;


};

CLICK_ENDDECLS
#endif
