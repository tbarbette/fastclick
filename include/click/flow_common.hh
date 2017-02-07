#ifndef CLICK_FLOWCOMMON_HH
#define CLICK_FLOWCOMMON_HH
#include <click/ring.hh>
#include <click/multithread.hh>

CLICK_DECLS

#ifdef HAVE_FLOW

class FlowControlBlock;
class FCBPool;
class FlowNode;

typedef union {
	uint8_t data_8;
	uint16_t data_16;
	uint32_t data_32;
	uint64_t data_64;
	void* data_ptr;
} FlowNodeData;

typedef void (*SubFlowRealeaseFnt)(FlowControlBlock* fcb);

class FlowControlBlock {

private:
	int use_count;

	public:

		/*#define FLOW_RELEASE			  0x01 //Release me when you got free time, my packets will never arrive again
		#define FLOW_PERMANENT 			  0x02 //Never release
		#define FLOW_TIMEOUT 		      0x80 //Timeout set
		#define FLOW_TIMEOUT_MASK	0xffffff00 //Delta + lastseen timestamp (max 16s)

		uint32_t flags;*/

		Timestamp lastseen;

		SubFlowRealeaseFnt release_fnt ;
		FCBPool* release_pool;

		FlowNode* parent;

        union {
            uint8_t data[0];
            uint16_t data_16[0];
            uint32_t data_32[0];
            uint64_t data_64[0];
            FlowNodeData node_data[0];
        };
        //No data after this

		inline void initialize() {
			use_count = 0;
		}

		inline void acquire(int packets_nr = 1) {
			use_count+=packets_nr;
		}

		inline void release(int packets_nr = 1);

		inline void clear() {
			use_count = 0;
		}

		inline bool released() {
			return use_count == 0;
		}

		inline int count() {
			return use_count;
		}

		inline FlowControlBlock* duplicate(int use_count);

		void print(String prefix);
		//Nothing after this line !
};

extern __thread FlowControlBlock* fcb_stack;

#define SFCB_STACK(fnt) \
		{FlowControlBlock* fcb_save = fcb_stack;fcb_stack=0;fnt;fcb_stack=fcb_save;}

#define SFCB_POOL_COUNT 32
#define SFCB_POOL_SIZE 2048
class FCBPool {
private:
    class SFCBList {

    public:
    	FlowControlBlock* p;
        unsigned count;
        operator int() const { return count; }
        SFCBList (int c ) : p(0), count( c ) {
        }
    	SFCBList() : p(0), count(0) {

    	}

        inline void add(FlowControlBlock* fcb) {
        	*((FlowControlBlock**)fcb) = p;
        	p = fcb;
        	count++;
        }

        inline FlowControlBlock* get() {
        	FlowControlBlock* fcb = p;
        	p = *((FlowControlBlock**)fcb);
        	count--;
        	fcb->clear();
        	return fcb;
        }
    };


	inline FlowControlBlock* alloc_new() {
		FlowControlBlock* fcb = (FlowControlBlock*)CLICK_LALLOC(sizeof(FlowControlBlock) + _data_size);
		fcb->release_fnt = 0;
		fcb->release_pool = this;
		fcb->clear();
		bzero(&fcb->data,_data_size);
		return fcb;
	}

	typedef MPMCRing<SFCBList,SFCB_POOL_COUNT> SFCBListRing;

	SFCBListRing global_fcb_list_ring;

	size_t _data_size;
	per_thread_oread<SFCBList> lists;
public:
	FCBPool() : _data_size(0), lists() {

	}

	~FCBPool() {
	    //TODO
	    /*
		//Free all per-thread caches
		for (unsigned i = 0; i < lists.weight(); i++) {
			SFCBList &list = lists.get_value(i);
			FlowControlBlock* fcb;
			while (list.count > 0 && (fcb = list.get()) != NULL) {
				CLICK_LFREE(fcb,sizeof(FlowControlBlock) + _data_size);
			}
		}

		//Free the caches inside the global ring
		SFCBList list;
		while ((list = global_fcb_list_ring.extract()) != 0) {
			FlowControlBlock* fcb;
			while (list.count > 0 && (fcb = list.get()) != NULL) {
				CLICK_LFREE(fcb,sizeof(FlowControlBlock) + _data_size);
			}
		}*/
	}


	void initialize(size_t data_size) {
		_data_size = data_size;
	}

	void compress(Bitvector threads) {
		lists.compress(threads);
		for (unsigned i = 0; i < lists.weight(); i++) {
			SFCBList &list = lists.get_value(i);
			for (int j = 0; j < SFCB_POOL_SIZE; j++) {
				FlowControlBlock* fcb = alloc_new();
				list.add(fcb);
			}
		}
	}

	inline FlowControlBlock* allocate() {
		if (lists->count > 0)
			return lists->get();

		SFCBList newlist;

		newlist = global_fcb_list_ring.extract();
		if (newlist.count) {
			lists->count = newlist.count;
			lists->p = newlist.p;
			return lists->get();

		} else {
			FlowControlBlock* fcb = alloc_new();
			return fcb;
		}
	}

	inline void release(FlowControlBlock* fcb) {
		if (lists->count >= SFCB_POOL_SIZE) {
			global_fcb_list_ring.insert(lists.get());
			lists->p = 0;
			lists->count = 0;
		}
		lists->add(fcb);
	}

	size_t data_size() {
		return _data_size;
	}
};

inline FlowControlBlock* FlowControlBlock::duplicate(int use_count) {
	FlowControlBlock* fcb = release_pool->allocate();
	//fcb->release_ptr = release_ptr;
	//fcb->release_fnt = release_fnt;
	memcpy(fcb, this ,sizeof(FlowControlBlock) + release_pool->data_size());
	fcb->use_count = use_count;
	return fcb;
}

inline void FlowControlBlock::release(int packets_nr) {

	if (use_count <= 0) {
		click_chatter("ERROR : negative release : release %p, use_count = %d",this,use_count);
		assert(use_count > 0);
	}
	//click_chatter("Release %d packets",packets_nr);
	use_count -= packets_nr;
	if (use_count <= 0) {
		//click_chatter("Release fcb %p",this);
		if (release_fnt)
			release_fnt(this);
		else
			click_chatter("No release fnt? Should not happen in practice...");
		release_pool->release(this);
	}
}
#else
#define SFCB_STACK(fnt) \
		{fnt;}
#endif //HAVE_FLOW
CLICK_ENDDECLS
#endif
