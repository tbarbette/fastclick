#ifndef CLICK_FLOWIPMANAGERMP_HH
#define CLICK_FLOWIPMANAGERMP_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/batchelement.hh>
#include <click/pair.hh>
#include <click/flow/common.hh>
#include <click/batchbuilder.hh>
#include "flowipmanager.hh"

CLICK_DECLS
class DPDKDevice;
struct rte_hash;

/**
 * =c
 * FlowIPManagerMP(...)
 *
 * =s flow
 *
 *  FCB packet classifier, cuckoo thread-safe implementation of DPDK.
 *
 * =d
 *  Multi-thread equivalent of FlowIPManager. This version uses DPDK's
 *  thread-safe implementation of cuckoo hash table to ensure thread safeness.
 *
 *  See @FlowIPManager documentation for usage.
 *
 * =a FlowIPManger
 */
class FlowIPManagerMP: public FlowIPManager {
    public:
        FlowIPManagerMP() CLICK_COLD;
        ~FlowIPManagerMP() CLICK_COLD;

        const char *class_name() const override { return "FlowIPManagerMP"; }
        const char *port_count() const override { return "1/1"; }
        const char *processing() const override { return PUSH; }
};

CLICK_ENDDECLS
#endif
