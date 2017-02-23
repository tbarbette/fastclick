#ifndef CLICK_FLOWCOMMON_HH
#define CLICK_FLOWCOMMON_HH
#include <click/ring.hh>
#include <click/multithread.hh>

CLICK_DECLS

#ifdef HAVE_FLOW

#define DEBUG_CLASSIFIER_MATCH 0 //0 no, 1 build-time only, 2 whole time, 3 print table after each dup
#define DEBUG_CLASSIFIER_RELEASE 0
#define DEBUG_CLASSIFIER_TIMEOUT 0
#define DEBUG_CLASSIFIER_TIMEOUT_CHECK 0 //1 check at release, 2 check at insert (big hit)
#define DEBUG_CLASSIFIER 0 //1 : Build-time only, >1 : whole time

#define HAVE_DYNAMIC_FLOW_RELEASE_FNT 0

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

        Timestamp lastseen; //Last seen is also used without sloppy timeout for cache purposes

#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
		//#define FLOW_RELEASE			  0x01 //Release me when you got free time, my packets will never arrive again
		//#define FLOW_PERMANENT 			  0x02 //Never release
        #define FLOW_TIMEOUT_INLIST       0x40 //Timeout is already in list
		#define FLOW_TIMEOUT              0x80 //Timeout set
        #define FLOW_TIMEOUT_SHIFT           8
		#define FLOW_TIMEOUT_MASK   0x000000ff //Delta + lastseen timestamp in msec

		uint32_t flags;

		FlowControlBlock* next;

        inline bool hasTimeout() {
            return flags & FLOW_TIMEOUT;
        }

		inline bool timeoutPassed(Timestamp now) {
		    unsigned t = (flags >> FLOW_TIMEOUT_SHIFT);
		    return t == 0 || (now - lastseen).msecval() > t;
		}
#endif

#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
		SubFlowRealeaseFnt release_fnt ;
		FCBPool* release_pool;
#endif

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
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
			flags = 0;
			next = 0; //TODO : only once?
#endif
		}

		inline void acquire(int packets_nr = 1);
		inline void release(int packets_nr = 1);

		inline void _do_release();

/*
		inline bool released() {
			return use_count == 0;
		}*/

		inline int count() {
			return use_count;
		}

		inline FlowControlBlock* duplicate(int use_count);
        inline FCBPool* get_pool() const;

		void print(String prefix);

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
        	fcb->initialize();
        	return fcb;
        }
    };


	inline FlowControlBlock* alloc_new() {
		FlowControlBlock* fcb = (FlowControlBlock*)CLICK_LALLOC(sizeof(FlowControlBlock) + _data_size);
#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
		fcb->release_fnt = 0;
		fcb->release_pool = this;
#endif
		fcb->initialize();
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
    FlowControlBlock* fcb;
    fcb = get_pool()->allocate();
	//fcb->release_ptr = release_ptr;
	//fcb->release_fnt = release_fnt;
	memcpy(fcb, this ,sizeof(FlowControlBlock) + get_pool()->data_size());
	fcb->use_count = use_count;
	return fcb;
}


class FlowTableHolder {
public:

    FlowTableHolder() :
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
old_flows(fcb_list()),
#endif
_pool(), _pool_release_fnt(0)
    {

    }
    ~FlowTableHolder() {

    }

    FCBPool* get_pool() {
        return &_pool;
    }


    void set_release_fnt(SubFlowRealeaseFnt pool_release_fnt);


    inline void release(FlowControlBlock* fcb) {
        if (likely(_pool_release_fnt))
            _pool_release_fnt(fcb);
        _pool.release(fcb);
    }

#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
    void release_later(FlowControlBlock* fcb);
    bool check_release();
    struct fcb_list {
        static const unsigned DEFAULT_THRESH = 64;
        fcb_list() : next(0), count(0), count_thresh(DEFAULT_THRESH) {

        }
        FlowControlBlock* next;
        unsigned count;
        unsigned count_thresh;

        inline FlowControlBlock* find(FlowControlBlock* fcb) {
            FlowControlBlock* head = next;
        #if DEBUG_CLASSIFIER_TIMEOUT > 3
            click_chatter("FIND head %p",head);
        #endif
            while (head != 0) {
                if (fcb == head)
                    return fcb;

                assert(head != head->next);
                head = head->next;
        #if DEBUG_CLASSIFIER_TIMEOUT > 3
                click_chatter("FIND next %p",head);
        #endif
            }
            return 0;
        }
    };
    per_thread<fcb_list> old_flows;
#endif

protected:
    FCBPool _pool;
    SubFlowRealeaseFnt _pool_release_fnt;

};


#if !HAVE_DYNAMIC_FLOW_RELEASE_FNT
extern __thread FlowTableHolder* fcb_table;
#endif

#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
        inline FCBPool* FlowControlBlock::get_pool() const {
            return release_pool;
        }
#else
        inline FCBPool* FlowControlBlock::get_pool() const {
            return fcb_table->get_pool();
        }
#endif

inline void FlowControlBlock::acquire(int packets_nr) {
            use_count+=packets_nr;
#if DEBUG_CLASSIFIER_RELEASE > 1
            click_chatter("Acquire %d to %p, total is %d",packets_nr,this,use_count);
#endif
        }

inline void FlowControlBlock::_do_release() {
#if DEBUG_CLASSIFIER_TIMEOUT_CHECK && HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
    assert(!(flags & FLOW_TIMEOUT_INLIST));
#endif
#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
       if (release_fnt)
           release_fnt(this);
       else
           click_chatter("No release fnt? Should not happen in practice...");
       get_pool()->release(this);
#else
       fcb_table->release(this);
#endif
}

inline void FlowControlBlock::release(int packets_nr) {
	if (use_count <= 0) {
		click_chatter("ERROR : negative release : release %p, use_count = %d",this,use_count);
		assert(use_count > 0);
	}
	use_count -= packets_nr;

#if DEBUG_CLASSIFIER_RELEASE > 1
            click_chatter("Release %d to %p, total is %d",packets_nr,this,use_count);
#endif

	if (use_count <= 0) {
		//click_chatter("Release fcb %p",this);
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
	    if (this->hasTimeout()) {
	        if (this->flags & FLOW_TIMEOUT_INLIST) {
#if DEBUG_CLASSIFIER_TIMEOUT > 2
                click_chatter("Not releasing %p because timeout is in list",this);
#if DEBUG_CLASSIFIER_TIMEOUT_CHECK > 1
                assert(fcb_table->old_flows.get().find(this));
#endif
#endif
	            return;
	        }

	        //Timeout is not in list yet
	        if (!this->timeoutPassed(Timestamp::recent_steady())) {
#if DEBUG_CLASSIFIER_TIMEOUT  > 2
	            click_chatter("Not releasing %p because timeout is not passed",this);
#endif
	            fcb_table->release_later(this);
	            return;
	        } else {
#if DEBUG_CLASSIFIER_TIMEOUT  > 2
	            unsigned t = (flags >> FLOW_TIMEOUT_SHIFT);
                click_chatter("Timeout %p has passed (%d/%d msec)!",this, (Timestamp::recent_steady() - lastseen).msecval(),t);
#endif
	        }
	    } else {
#if DEBUG_CLASSIFIER_TIMEOUT  > 2
                click_chatter("No timeout for fcb %p",this);
#endif
	    }
#endif
	    this->_do_release();

	}
}

#else
#define SFCB_STACK(fnt) \
		{fnt;}
#endif //HAVE_FLOW
CLICK_ENDDECLS
#endif
