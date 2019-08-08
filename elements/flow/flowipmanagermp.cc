/*
 * FlowIPManagerMP.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include "flowipmanagermp.hh"
#include <rte_hash.h>

CLICK_DECLS

FlowIPManagerMP::FlowIPManagerMP() {
	_flags = RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD | RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY;
}

FlowIPManagerMP::~FlowIPManagerMP() {

}


CLICK_ENDDECLS

EXPORT_ELEMENT(FlowIPManagerMP)
ELEMENT_MT_SAFE(FlowIPManagerMP)
