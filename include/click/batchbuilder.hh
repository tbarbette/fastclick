#ifndef CLICK_BATCHBUILDER_HH
#define CLICK_BATCHBUILDER_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/vector.hh>
#include <click/ipflowid.hh>

CLICK_DECLS
class DPDKDevice;

struct BatchBuilder {
	BatchBuilder() : first(0), count(0), last(-1), last_id() {

	};

	Packet* first;
	Packet* tail;
	int count;
	int last;
    IPFlow5ID last_id;

	inline void init() {
		count = 0;
		first = 0;
	}

	inline PacketBatch* finish() {
		if (!first)
			return 0;
		return PacketBatch::make_from_simple_list(first,tail,count);
	}

	inline void append(Packet* p) {
		count++;
		if (first) {
			tail->set_next(p);
			tail = p;
		} else {
			first = p;
			tail = p;
		}
	}

};


CLICK_ENDDECLS
#endif
