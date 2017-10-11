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

#define DEBUG_CLASSIFIER_CHECK 0

#if DEBUG_CLASSIFIER
    #define debug_flow(...) click_chatter(__VA_ARGS__);
#else
    #define debug_flow(...)
#endif

#if DEBUG_CLASSIFIER_CHECK || DEBUG_CLASSIFIER
    #define flow_assert(...) assert(__VA_ARGS__);
#else
    #define flow_assert(...)
#endif

#define HAVE_DYNAMIC_FLOW_RELEASE_FNT 1

class FlowControlBlock;
class FCBPool;
class FlowNode;

union FlowNodeData{
	uint8_t data_8;
	uint16_t data_16;
	uint32_t data_32;
	uint64_t data_64;
	void* data_ptr;

	FlowNodeData() : data_64(0) {};
	FlowNodeData(uint8_t d) : data_8(d) {};
	FlowNodeData(uint16_t d) : data_16(d) {};
	FlowNodeData(uint32_t d) : data_32(d) {};
	FlowNodeData(uint64_t d) : data_64(d) {};
};

typedef void (*SubFlowRealeaseFnt)(FlowControlBlock* fcb, void* thunk);
struct FlowReleaseChain {
    SubFlowRealeaseFnt previous_fnt;
    void* previous_thunk;
};

class FlowControlBlock {

private:
	int use_count;

	public:

        Timestamp lastseen; //Last seen is also used without sloppy timeout for cache purposes

#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
		//#define FLOW_RELEASE			  0x01 //Release me when you got free time, my packets will never arrive again
		//#define FLOW_PERMANENT 			  0x02 //Never release
        //0x20 is reserved, see below
        #define FLOW_TIMEOUT_INLIST       0x40 //Timeout is already in list
		#define FLOW_TIMEOUT              0x80 //Timeout set
        #define FLOW_TIMEOUT_SHIFT           8
		#define FLOW_TIMEOUT_MASK   0x000000ff //Delta + lastseen timestamp in msec

		FlowControlBlock* next;

        inline bool hasTimeout() const {
            return flags & FLOW_TIMEOUT;
        }

		inline bool timeoutPassed(Timestamp now) const {
		    unsigned t = (flags >> FLOW_TIMEOUT_SHIFT);
		    return t == 0 || (now - lastseen).msecval() > t;
		}
#endif

        #define FLOW_EARLY_DROP     0x20
        uint32_t flags;

        inline void set_early_drop(bool set=true) {
            if (set)
                flags |= FLOW_EARLY_DROP;
            else
                flags &= ~FLOW_EARLY_DROP;
        }

        inline bool is_early_drop() const {
            return flags & FLOW_EARLY_DROP;
        }



#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
		SubFlowRealeaseFnt release_fnt ;
		void* thunk;
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

        void combine_data(uint8_t* data);
		inline void initialize() {
			use_count = 0;
            flags = 0;
            release_fnt = 0;
            thunk = 0;
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
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

		inline int count() const {
			return use_count;
		}

		FlowControlBlock* duplicate(int use_count);
        inline FCBPool* get_pool() const;

		void print(String prefix, int data_offset =-1, bool show_ptr=false) const;

		bool empty();

		/**
		 * Check consistency
		 */
		void check() {
		}

};

class FlowTableHolder;

extern __thread FlowControlBlock* fcb_stack;
extern __thread FlowTableHolder* fcb_table;



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
/*#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
		fcb->release_fnt = &pool_release_fnt;
		fcb->thunk = this;
#endif*/
		fcb->initialize();
		return fcb;
	}

	typedef MPMCRing<SFCBList,SFCB_POOL_COUNT> SFCBListRing;

	SFCBListRing global_fcb_list_ring;


	size_t _data_size;
	per_thread_oread<SFCBList> lists;
public:
    static FCBPool* biggest_pool;
    static int initialized;
    static int init_data_size() {
        return biggest_pool->data_size();
    }

    static FlowControlBlock* init_allocate();
    static void init_release(FlowControlBlock*);

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


	void initialize(size_t data_size, FlowTableHolder* table) {
		_data_size = data_size;
		if (!biggest_pool || biggest_pool->data_size() < data_size) {
		    biggest_pool = this;
		    fcb_table = table;
		}
		FCBPool::initialized++;
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

    inline FlowControlBlock* allocate_empty() {
        FlowControlBlock* fcb = allocate();
        bzero(fcb->data + sizeof(FlowNodeData), data_size() - sizeof(FlowNodeData));
        return fcb;
    }

	inline void release(FlowControlBlock* fcb) {
		if (lists->count >= SFCB_POOL_SIZE) {
			global_fcb_list_ring.insert(lists.get());
			lists->p = 0;
			lists->count = 0;
		}
		lists->add(fcb);
	}

	static void pool_release_fnt(FlowControlBlock* fcb, void* thunk) {
	    static_cast<FCBPool*>(thunk)->release(fcb);
	}

	size_t data_size() {
		return _data_size;
	}
};

class FlowTableHolder {
public:

    FlowTableHolder() :
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
old_flows(fcb_list()),
#endif
_pool(), _classifier_release_fnt(0), _classifier_thunk(0)
    {

    }
    ~FlowTableHolder() {

    }

    FCBPool* get_pool() {
        return &_pool;
    }


    void set_release_fnt(SubFlowRealeaseFnt pool_release_fnt, void* thunk);


    inline void release(FlowControlBlock* fcb) {
        if (likely(_classifier_release_fnt)) //Release the FCB from the tree
            _classifier_release_fnt(fcb, _classifier_thunk);
        _pool.release(fcb); //Release the FCB itself inside the pool
    }

#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
    void release_later(FlowControlBlock* fcb);
    bool check_release();
    struct fcb_list {
        static const unsigned DEFAULT_THRESH = 64;
        fcb_list() : _next(0), _count(0), _count_thresh(DEFAULT_THRESH) {

        }
        FlowControlBlock* _next;
        unsigned _count;
        unsigned _count_thresh;

        unsigned count() const {
            return _count;
        }

        inline FlowControlBlock* find(FlowControlBlock* fcb) {
            FlowControlBlock* head = _next;
        #if DEBUG_CLASSIFIER_TIMEOUT > 3
            click_chatter("FIND head %p",head);
        #endif
            while (head != 0) {
                if (fcb == head)
                    return fcb;

                assert(head != head->next);
                head = head->next;
        #if DEBUG_CLASSIFIER_TIMEOUT > 5
                click_chatter("FIND next %p",head);
        #endif
            }
            return 0;
        }

        inline int find_count() {
            FlowControlBlock* head = _next;
            int i = 0;
            while (head != 0) {
                i++;
                head = head->next;
            }
            return i;
        }

    };
    per_thread<fcb_list> old_flows;
#endif

protected:
    FCBPool _pool;
    SubFlowRealeaseFnt _classifier_release_fnt;
    void* _classifier_thunk;

};


//#if !HAVE_DYNAMIC_FLOW_RELEASE_FNT
extern __thread FlowTableHolder* fcb_table;
//#endif

/*#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
        inline FCBPool* FlowControlBlock::get_pool() const { //Only works at initialisation
            return static_cast<FCBPool*>(thunk);
        }
#else*/
        inline FCBPool* FlowControlBlock::get_pool() const {
            return fcb_table->get_pool();
        }
//#endif

 void FlowControlBlock::acquire(int packets_nr) {
            use_count+=packets_nr;
#if DEBUG_CLASSIFIER_RELEASE > 1
            click_chatter("Acquire %d to %p, total is %d",packets_nr,this,use_count);
#endif
        }

inline void FlowControlBlock::_do_release() {
#if DEBUG_CLASSIFIER
    assert(!(flags & FLOW_TIMEOUT_INLIST));
    //click_chatter("Release fnt is %p",release_fnt);
#endif
    SFCB_STACK(
#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
       if (release_fnt)
           release_fnt(this, thunk);
#endif
    //click_chatter("Releasing from table");
    fcb_table->release(this);
    );
}

inline void FlowControlBlock::release(int packets_nr) {
	if (use_count - packets_nr  < 0) {
		click_chatter("ERROR : negative release : release %p, use_count = %d, releasing %d",this,use_count,packets_nr);
		assert(use_count - packets_nr >= 0);
	}
	use_count -= packets_nr;

#if DEBUG_CLASSIFIER_RELEASE > 1
            click_chatter("Release %d to %p, total is %d",packets_nr,this,use_count);
#endif

	if (use_count == 0) {
	    debug_flow("Release fcb %p, uc 0, hc %d",this,hasTimeout());
		//assert(this->hasTimeout());
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
	    if (this->hasTimeout()) {
	        if (this->flags & FLOW_TIMEOUT_INLIST) {
#if DEBUG_CLASSIFIER_TIMEOUT > 2
                click_chatter("Not releasing %p because timeout is in list, uc %d",this,use_count);
#endif
#if DEBUG_CLASSIFIER_TIMEOUT_CHECK > 1
                assert(fcb_table->old_flows.get().find(this));
#endif
	            return;
	        }
#if DEBUG_CLASSIFIER_TIMEOUT_CHECK > 1
            assert(!fcb_table->old_flows.get().find(this));
#endif
	        //Timeout is not in list yet
	        if (!this->timeoutPassed(Timestamp::recent_steady())) {
#if DEBUG_CLASSIFIER_TIMEOUT  > 2
	            click_chatter("Not releasing %p because timeout is not passed. Adding to the list",this);
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
#if DEBUG_CLASSIFIER_RELEASE
	else {
	    click_chatter("NO RELEASE, fcb %p, uc %d",this,this->use_count);
	}
#endif
}


#else
#define SFCB_STACK(fnt) \
		{fnt;}
#endif //HAVE_FLOW
CLICK_ENDDECLS
#endif
