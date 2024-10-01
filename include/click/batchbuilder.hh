#ifndef CLICK_BATCHBUILDER_HH
#define CLICK_BATCHBUILDER_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/vector.hh>
#include <click/ipflowid.hh>

CLICK_DECLS
class DPDKDevice;


struct PacketList {
	PacketList() : first(0), count(0) {

	};

	Packet* first;
	Packet* tail;
	int count;

	/**
	 * @brief Finish the construction of the batch. It is *not* reusable without calling init.
	 *
	 * @return PacketBatch* A batch with all packets queued
	 */
	inline PacketBatch* finish() {
		if (!first)
			return 0;
		return PacketBatch::make_from_simple_list(first,tail,count);
	}


	/**
	 * @brief Extract all packets. The builder can be reused without calling init.
	 *
	 * @return PacketBatch* A batch with all packets queued
	 */
	inline PacketBatch* pop_all() {
		if (!first)
			return 0;
		PacketBatch* b = PacketBatch::make_from_simple_list(first,tail,count);
		first = 0;
		count = 0;
		return b;
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

	inline bool empty() const {
		return first == 0;
	}

	inline Packet* front() const {
		return first;
	}

	inline Packet* pop_front() {
		Packet* f = first;
		first = f->next();
		count--;
		return f;
	}
};

struct BatchBuilder : PacketList {
	BatchBuilder() : last(-1), last_id() {

	};

	int last;
    IPFlow5ID last_id;

	inline void init() {
		count = 0;
		first = 0;
	}
};


CLICK_ENDDECLS
#endif
