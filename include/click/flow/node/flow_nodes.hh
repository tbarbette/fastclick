#ifndef CLICK_FLOWNODES_HH
#define CLICK_FLOWNODES_HH 1

#include <click/allocator.hh>
#include <click/straccum.hh>

#include "../level/flow_level.hh"

#define FLOW_NODE_DEFINE(T,fnt) \
        static FlowNodePtr* find_ptr(void* thunk, FlowNodeData data, bool &need_grow) {\
            return static_cast<T*>(thunk)->fnt(data,need_grow);\
        }


class FlowNode {
private:

protected:
    union {
        FlowLevel* _level;
        void* pool_next_item;
    };
    union {
        FlowNode* _parent;
        void* pool_next_pool;
    };
    union {
        uint32_t poison;
        uint32_t num;
    };
    FlowNodePtr _default;


    bool _growing;

    typedef FlowNodePtr* (*FindFn)(void*,FlowNodeData data,bool &need_grow);

    FindFn _find;

#if FLOW_KEEP_STRUCTURE
    bool _released;
#endif

    void duplicate_internal(FlowNode* node, bool recursive, int use_count, bool duplicate_leaf) {
        assign(node);
        if (unlikely(recursive)) {
            this->_level = node->level()->duplicate();
            if (_default.ptr) {
                if ( _default.is_node()) {
                    _default.set_node(node->_default.node->duplicate(recursive, use_count, duplicate_leaf));

                    _default.set_parent(this);
                } else {
                    if (duplicate_leaf) {
                        _default.set_leaf(_default.leaf->duplicate(use_count));
                        _default.set_parent(this);
                    } else {
                        _default.set_leaf(_default.leaf);
                     }
                }
            }

            NodeIterator it = node->iterator();
            FlowNodePtr* child;
            while ((child = it.next()) != 0) {
                if (child->is_leaf()) {
                    if (duplicate_leaf) {
                        FlowControlBlock* new_leaf = child->leaf->duplicate(use_count);
                        new_leaf->parent = this;
                        add_leaf(child->data(),new_leaf);
                    } else {
                        add_leaf(child->data(),child->leaf);
                    }
                } else {
                    FlowNode* new_node = child->node->duplicate(recursive, use_count, duplicate_leaf);
                    new_node->set_parent(this);
                    add_node(child->data(),new_node);
                }
            }
#if DEBUG_CLASSIFIER
            this->assert_diff(node);
#endif
        }
    }

    /**
     * Testing function to check if a packet is matching the given leaf in its parent
     * True if matches
     */
    inline bool _leaf_reverse_match(FlowControlBlock* &leaf, Packet* &p) {
        flow_assert(level());
        if (default_ptr()->ptr == leaf) { //If default, we need to check it does not match any non-default
#if DEBUG_CLASSIFIER_MATCH > 1
            click_chatter("%d -> %p %p",level()->get_data(p),this->find_or_default(level()->get_data(p))->ptr,leaf);
#endif
            return this->find_or_default(level()->get_data(p))->ptr == leaf;
        } else {
            return level()->get_data(p).equals(leaf->get_data());
        }
    }

    /**
     * Testing function to ensure that a node is matching this packet in its parent
     */
    inline bool _node_reverse_match(FlowNode* &child, Packet* &p) {
        if (default_ptr()->ptr == child) //If default, we need to check it does not match any non-default
            return this->find_or_default(level()->get_data(p))->ptr == child;
        else
            return level()->get_data(p).equals(child->node_data);
    }

    /**
     * Check that a duplication is correct
     * @param node
     */
    void assert_diff(FlowNode* node) {
        assert(node->level() != this->level());
        FlowNode::NodeIterator it = this->iterator();
        FlowNode::NodeIterator ito = node->iterator();
        FlowNodePtr* child;
        FlowNodePtr* childo;
        while ((child = it.next()) != 0) {
            childo = ito.next();
            assert(child != childo);
            assert(child->is_leaf() == childo->is_leaf());
            if (child->is_leaf()) {
            } else {
                child->node->assert_diff(childo->node);
            }
        }
        assert(this->default_ptr() != node->default_ptr());
    }

public:
    FlowNodeData node_data;

#if DEBUG_CLASSIFIER
    Bitvector threads;
#endif

    /**
     * Destroy release memory efficiently (may call a pool instead of pure delete)
     * Will kill all children, including default
     */
    virtual void destroy() {
        delete this;
    }


    FlowNode() :  num(0), _level(0), _default(), _parent(0), _growing(false), node_data()
#if FLOW_KEEP_STRUCTURE
            ,_released(false)
#endif
    {
    }

    static FlowNode* create_hash(int l);
    FlowLevel* level() const {
        return _level;
    }

    void set_level(FlowLevel* level) {
        _level = level;
    }

    inline void add_node(FlowNodeData data, FlowNode* node) {
        bool need_grow;
        FlowNodePtr* ptr = _find(this,data,need_grow);
        if (ptr->ptr != 0) {
            click_chatter("FATAL ERROR : Adding node to an existing node");
            print();
            node->print();
            flow_assert(ptr->ptr == 0);
        }
        inc_num();
        ptr->set_node(node);
        ptr->set_data(data);
    }

    inline void add_leaf(FlowNodeData data, FlowControlBlock* leaf) {
        bool need_grow;
        FlowNodePtr* ptr = _find(this,data,need_grow);
        flow_assert(ptr->ptr == 0);
        inc_num();
        ptr->set_leaf(leaf);
        ptr->set_data(data);
    }

    /**
     * Run FNT on all children (leaf or nodes, but not empties)
     */
    void apply(std::function<void(FlowNodePtr*)> fnt);
    /**
     * Run FNT on all children and the default if it's not empty,
     */
    void apply_default(std::function<void(FlowNodePtr*)> fnt);

    FlowNode* combine(FlowNode* other, bool as_child, bool priority, bool duplicate_leaf, Element* origin) CLICK_WARN_UNUSED_RESULT;
    void __combine_child(FlowNode* other, bool priority, bool duplicate_leaf, Element* origin);
    void __combine_else(FlowNode* other, bool priority, bool duplicate_leaf, Element* origin);
    FlowNodePtr prune(FlowLevel* level,FlowNodeData data, bool inverted, bool &changed) CLICK_WARN_UNUSED_RESULT;

    FlowNode* find_node(FlowNode* other);

    virtual FlowNode* duplicate(bool recursive, int use_count, bool duplicate_leaf) = 0;

    void assign(FlowNode* node) {
        _level = node->_level;
        _parent = node->_parent;
        _default = node->_default;
    }

    int getNum() const { return num; };

    //To use for testing purposes only
    int findGetNum();

    virtual ~FlowNode() {
/*        if (_default.ptr && _default.is_node()) {
            click_chatter("%p", _default.node);
            delete _default.node;
        }*/
        //Default may be referenced multiple times in dynamics, TODO: delete from somewhere else
    };

#if FLOW_KEEP_STRUCTURE
    inline bool released() const {
        return _released;
    }

    inline void release() {
        flow_assert(!growing());
        _released = true;
#if DEBUG_CLASSIFIER
        apply([](FlowNodePtr* p) {
            if (p->ptr && p->is_node()) {
#if FLOW_KEEP_STRUCTURE
                flow_assert(p->node->released());
#endif
            }
        });
#endif
    };

#endif

    inline FlowNodePtr* find(FlowNodeData data, bool &need_grow) {
        return _find((void*)this,data,need_grow);
    }


    inline FlowNodePtr* find_or_default(const FlowNodeData data) {
        bool need_grow;
        FlowNodePtr* ptr = _find(this,data,need_grow);
        if (ptr->ptr == 0
#if FLOW_KEEP_STRUCTURE
                || (ptr->is_node() && ptr->node->released())
#endif
                )
            return default_ptr();
        return ptr;
    }

    void inc_num() {
        num++;
    }

    void dec_num() {
        num--;
    }

    inline FlowNode* parent() const {
        return _parent;
    }

    inline FlowNodePtr get_default() {
        return _default;
    }

    inline FlowNodePtr* default_ptr() {
        return &_default;
    }


    inline void set_default(FlowNodePtr ptr) {
        if (ptr.ptr) {
            flow_assert(ptr.ptr != this);
            _default = ptr;
            _default.set_parent(this);
        }
        flow_assert(_default.ptr == ptr.ptr);
    }


    inline void set_default(FlowNode* node) {
        flow_assert(_default.ptr == 0);
        flow_assert(node != this);
        _default.ptr = node;
        node->set_parent(this);
    }

    virtual void release_child(FlowNodePtr fl, FlowNodeData data) = 0;

#if FLOW_KEEP_STRUCTURE
    inline void renew() {
        _released = false;
    }
#endif

    virtual String name() const = 0;

    class NodeIteratorBase {
    public:
        virtual ~NodeIteratorBase() = default;
        virtual FlowNodePtr* next() = 0;
    };

    class NodeIterator {
    private:
        NodeIteratorBase* _base;
    public:
        NodeIterator(NodeIteratorBase* b) {
            _base = b;
        }
        ~NodeIterator(){
            delete _base;
        }
        FlowNodePtr* next() {
            return _base->next();
        }
        NodeIterator operator=(const NodeIterator& it) {
            NodeIterator nit(it._base);
            _base = 0;
            return nit;
        }
    };

    FlowNode* root() {
        FlowNode* p = _parent;
        while (p->parent() != 0) {
            p = p->parent();
        }
        return p;
    }
    virtual NodeIterator iterator() = 0;

    inline void set_parent(FlowNode* parent) {
        _parent = parent;
    }


    bool growing() const {
        return _growing;
    }

    void set_growing(bool g) {
        //assert(g); //There is no stopping of growing, when it stops, the table should be deleted,well we reset it also
        _growing = g;
    }
    FlowNode* start_growing(bool impl);

#if DEBUG_CLASSIFIER || DEBUG_CLASSIFIER_CHECK
    void check(bool allow_parent = false, bool allow_default=true, bool multithread=false);
#else
    inline void check(bool allow_parent = false, bool allow_default=true) {};
#endif

    virtual FlowNode* optimize(Bitvector threads) CLICK_WARN_UNUSED_RESULT;

    void traverse_all_leaves(std::function<void(FlowNodePtr*)> fnt, bool do_final, bool do_default);

    void traverse_all_leaves_and_empty_default(std::function<void(FlowNodePtr*,FlowNode*)> fnt, bool do_final, bool do_default);

    bool traverse_all_default_leaf(std::function<bool(FlowNode*)> fnt);

    bool traverse_all_nodes(std::function<bool(FlowNode*)> fnt);

    void traverse_parents(std::function<bool(FlowNode*)> fnt);

    void traverse_parents(std::function<void(FlowNode*)> fnt);

    bool is_dummy();

    bool is_full_dummy();

    bool has_no_default(bool allow_dynamic = false);

    void reverse_print();
    static void print(const FlowNode* node,String prefix,int data_offset = -1, bool show_ptr = true, bool recursive=true, bool do_release = true);

    static Spinlock printlock;
    inline void print(int data_offset = -1, bool show_ptr = true, bool recursive = true, bool do_release = true) const {
#if DEBUG_CLASSIFIER > 2
	printlock.acquire();
#endif
        click_chatter("---");
        print(this,"", data_offset, show_ptr, recursive, do_release);
        click_chatter("---");
#if DEBUG_CLASSIFIER > 2
	printlock.release();
#endif
    }
#if DEBUG_CLASSIFIER
    void debug_print(int data_offset = -1) const {
        print(data_offset,true);
    }
#else
    void debug_print(int = -1) const {}
#endif
private:
    void leaf_combine_data(FlowControlBlock* leaf, bool do_final, bool do_default, Element* origin);
    void leaf_combine_data_create(FlowControlBlock* leaf, bool do_final, bool do_default, bool discard_my_data, Element* origin);
    FlowNodePtr prune(FlowNode* parent, FlowNodeData data);
    FlowNodePtr prune_with_parent(FlowNode* parent, FlowNodeData data, bool was_default);
    FlowNode* replace_leaves(FlowNode* other, bool do_final, bool do_default, bool discard_my_fcb_data, Element* origin);
    friend class FlowClassificationTable;
    friend class FlowNodePtr;
    friend class FlowNodeDefinition;

template <typename T, int POOL_SIZE, int POOL_COUNT>
    friend class pool_allocator_aware_mt;

template<class T>
    friend class FlowAllocator;
};


/**
 * Node implemented using a linkedlist
 *//*
class FlowNodeList : public FlowNode {
	std::list<FlowNode*> children;
	int highwater = 16;
public:

	FlowNodeList() {};

	FlowNode* find(FlowNodeData data) {

		for (auto it = children.begin();it != children.end(); it++)
			if ((*it)->data == data)
				return *it;
		return NULL;
	}

	void set(uint32_t data, FlowNode* fl) {
		children.push_back(fl);
		num++;
		if (_child_deletable && num > highwater) {

			int freed = 0;
			for (auto it = children.begin();it != children.end(); it++) {

				if ((*it)->released()) {
					_remove((*it));
					freed++;
				}
			}
			if (!freed) highwater += 16;
		}
	}


	void _remove(FlowNode* child) {
		delete child;
		children.remove(child);
		num--;
	}

	virtual void release_child(FlowNode* child, FlowNodeData data) {
		_remove(child);
	}

	virtual ~FlowNodeList() {
		//click_chatter("Delete list");
		for (auto it = children.begin(); it != children.end(); it++)
			delete (*it);
	}
};*/

/**
 * Node implemented using a heap, static node only
 */
class FlowNodeHeap: public FlowNode  {
    Vector<FlowNodePtr> children;

public:

    FlowNodeHeap() {
        _find = &find_ptr;
    }

    void append_heap(FlowNode* fn,Vector<uint32_t>& vls, int i, int v_left, int v_right);
    void initialize(FlowNode*);

    FLOW_NODE_DEFINE(FlowNodeHeap,find_heap);

#define left_idx(i) (i*2 + 1)
#define right_idx(i) (i*2 + 2)

    inline FlowNodePtr* find_heap(FlowNodeData data, bool &need_grow) {
        int i = 0;
        while (FLOW_INDEX(children,i).ptr) {
            uint32_t cd = FLOW_INDEX(children,i).data().data_32;
            if (cd == data.data_32)
                return &FLOW_INDEX(children,i);
            else if (cd < data.data_32) {
                i = right_idx(i);
            } else {
                i = left_idx(i);
            }
        }
        return &FLOW_INDEX(children,i);
    }

    String name() const {
        return "HEAP";
    }

    void release_child(FlowNodePtr child, FlowNodeData data) override {
        click_chatter("TODO : release child heap");
        abort();
    }

    ~FlowNodeHeap();

    FlowNode* duplicate(bool recursive, int use_count, bool duplicate_leaf) override;

    class HeapNodeIterator : public NodeIteratorBase {
        FlowNodeHeap* _node;
        int cur;
    public:
        HeapNodeIterator(FlowNodeHeap* n) : cur(0) {
            _node = n;
        }

        FlowNodePtr* next() {
            do {
                if (cur >= _node->children.size())
                    return 0;
                if (_node->children[cur].ptr)
                    return &_node->children[cur++];
                cur++;
            }
            while (1);
        }
    };

    NodeIterator iterator() override {
        return NodeIterator(new HeapNodeIterator(this));
    }
};


/**
 * Node implemented using an array
 */
class FlowNodeArray : public FlowNode  {
    Vector<FlowNodePtr> children;

public:
    void destroy() override;

    FlowNodeArray() {
        _find = &find_ptr;
    }

    void initialize(unsigned int max_size) {
        children.resize(max_size);
    }

    FLOW_NODE_DEFINE(FlowNodeArray,find_array);

    inline FlowNodePtr* find_array(FlowNodeData data, bool &need_grow) {
        return &FLOW_INDEX(children,data.data_32);
    }

    String name() const {
        return "ARRAY";
    }
    void release_child(FlowNodePtr child, FlowNodeData data) override;


    ~FlowNodeArray();

    FlowNode* duplicate(bool recursive, int use_count, bool duplicate_leaf) override;

    class ArrayNodeIterator : public NodeIteratorBase {
        FlowNodeArray* _node;
        int cur;
    public:
        ArrayNodeIterator(FlowNodeArray* n) : cur(0) {
            _node = n;
        }

        FlowNodePtr* next() {
            do {
                if (cur >= _node->children.size())
                    return 0;
                if (_node->children[cur].ptr)
                    return &_node->children[cur++];
                cur++;
            }
            while (1);
        }
    };

    NodeIterator iterator() override {
        return NodeIterator(new ArrayNodeIterator(this));
    }
};

static const uint8_t HASH_SIZES_NR = 10;

//const int FlowNodeHash::hash_sizes[FlowNodeHash::HASH_SIZES_NR] = {256,512,1024,2048,4096,8192};


/**
 * Node implemented using a linear hash table
 */
template<int capacity_n>
class FlowNodeHash : public FlowNode  {

#if FLOW_HASH_RELEASE == RELEASE_EPOCH
    uint64_t epoch;
    #define MAX_EPOCH 16777216
    #define SET_DESTRUCTED_NODE(ptr,parent) (ptr = (void*)parent->epoch)
    #define IS_EMPTY_PTR(ptr,parent) ((uint64_t)ptr < parent->epoch)
    #define IS_DESTRUCTED_PTR(ptr,parent) ((uint64_t)ptr == parent->epoch)
    #define IS_FREE_PTR(ptr,parent) ((uint64_t)ptr <= parent->epoch)
    #define IS_FREE_PTR_ANY(ptr) ((uint64_t)ptr < MAX_EPOCH)
#else
    #define IS_EMPTY_PTR(ptr,parent)  (ptr == 0)
# if HAVE_FLOW_DYNAMIC
    #define DESTRUCTED_NODE (void*)1
    #define IS_FREE_PTR(ptr,parent) (IS_EMPTY_PTR(ptr,parent) || IS_DESTRUCTED_PTR(ptr,parent))
    #define IS_FREE_PTR_ANY(ptr)    (!((uintptr_t)ptr & (uintptr_t)-2))
# else
    #define DESTRUCTED_NODE (void*)0
    #define IS_FREE_PTR(ptr,parent) (IS_EMPTY_PTR(ptr,parent))
    #define IS_FREE_PTR_ANY(ptr)    (ptr == 0)
# endif
    #define SET_DESTRUCTED_NODE(ptr,parent) (ptr = DESTRUCTED_NODE)
    #define IS_DESTRUCTED_PTR(ptr,parent) (ptr == DESTRUCTED_NODE)
#endif
    #define IS_VALID_PTR(ptr,parent) (!IS_FREE_PTR(ptr,parent))
    #define IS_VALID_NODE(pptr,parent) (IS_VALID_PTR(pptr.ptr,parent) && pptr.is_node())

    static constexpr uint32_t hash_sizes[HASH_SIZES_NR] = {257,521,1031,2053,4099,8209,16411,32771,65539,131072}; //Prime for less collisions, after the last we double
    static constexpr uint32_t step_sizes[HASH_SIZES_NR] = { 37, 67, 131, 257, 521,1031, 2053, 4099, 8209, 16411}; //Prime for less collisions, after the last we double


    /**
     * @invariant capacity() < INT_MAX/2
     */
    inline static constexpr uint32_t capacity() {
        return hash_sizes[capacity_n];
    }

    /**
     * @invariant step() < capacity()
     */
    inline static constexpr uint32_t step() {
        return step_sizes[capacity_n];
    }

    FlowNodePtr children[capacity()];

    inline constexpr uint32_t max_highwater() const {
        return 3 * (capacity() / 5);
    }

    inline constexpr uint32_t collision_threshold() const {
        return ((capacity() / 20) > 32 ? 32 : capacity()/20);
    }

    inline constexpr uint32_t hole_threshold() const {
        return ((capacity() / 30) > 24 ? 24 : capacity()/30);
    }

    unsigned int hash32(uint32_t d) const {
        return ((d << 3) + (d >> 9) + (d >> 12) + (d >> 25)) % capacity();
    }

#if HAVE_LONG_CLASSIFICATION
    unsigned int hash64(uint64_t ld) const {
        uint32_t d = (uint32_t)ld ^ (ld >> 32);
        return ((d << 3) + (d >> 9) + (d >> 12) + (d >> 25)) % capacity();
    }
#endif

    inline int next_idx(int idx) const {
        idx = (idx + step());
        if (idx >= capacity()) //This is permitied per step() and capacity() invariants
            return idx - capacity();
        return idx;
    }

    inline int prev_idx(int idx) const {
        idx = (idx - step());
        if (idx < 0)
            return idx + capacity();
        return idx;

    }


    String name() const {
        return "HASH-" + String(capacity());
    }


    class HashNodeIterator : public virtual NodeIteratorBase {
        FlowNodeHash* _node;
        unsigned cur;
    public:
        HashNodeIterator(FlowNodeHash* n) : cur(0) {
            _node = n;
        }

        virtual FlowNodePtr* next() override {
            while (cur < _node->capacity() && (IS_FREE_PTR(_node->children[cur].ptr,_node))) {
                cur++;
            }
            if (cur >= _node->capacity())
                return 0;
            return &_node->children[cur++];
        }
    };

    void destroy() override;

    //Resizing is now done by the growing system
    void resize() {
        /*
		int current_size = hash_size;
		if (size_n < HASH_SIZES_NR)
			hash_size = hash_sizes[size_n];
		else {
			hash_size *= 2;
		}
		mask = hash_size - 1;

		highwater = hash_size / 3;
		max_highwater = hash_size / 2;
		click_chatter("New hash table size %d",hash_size);

		Vector<FlowNodePtr> children_old = children;
		children.resize(hash_size);
		num = 0;

		for (int i = 0; i < current_size; i++) {
			children[i].ptr= NULL;
		}
		size_n++;
		for (int i = 0; i < current_size; i++) {
			if (children_old[i].ptr) {
				if (children_old[i].is_leaf()) {
				GET IDX CANNOT BE USED FOR OLD
					children[get_idx(children_old[i].data())] = children_old[i];
				} else {
					if (children_old[i].node->released()) {
						delete children_old[i].node;
					} else {
						children[get_idx(children_old[i].data())] = children_old[i];
					}
				}
			}
		}*/
    }

    public:
    FlowNodeHash()
#if FLOW_HASH_RELEASE == RELEASE_EPOCH
        : epoch(1)
#endif
    {
        _find = &find_ptr;
#if DEBUG_CLASSIFIER
        for (int i = 0; i < capacity(); i++) {
            flow_assert(children[i].ptr == NULL);
        }
#endif
    };
    virtual FlowNode* duplicate(bool recursive, int use_count, bool duplicate_leaf) override;

    FLOW_NODE_DEFINE(FlowNodeHash,find_hash);

    static uint32_t capacity_for(int level) {
        return level << (8+level);
    }

    FlowNodePtr* find_hash(FlowNodeData data, bool &need_grow);

    void release_child(FlowNodePtr child, FlowNodeData data);

    virtual ~FlowNodeHash() override;

    NodeIterator iterator() override {
        return NodeIterator(new HashNodeIterator(this));
    }
};




/**
 * Dummy node for root with one child
 */
class FlowNodeDummy : public FlowNode {

    class DummyIterator : public NodeIteratorBase{
        FlowNode* _node;
    public:
        DummyIterator(FlowNode* n) {
            _node = n;
        }

        FlowNodePtr* next() {
            return 0;
        }
    };

    public:

    String name() const {
        return "DUMMY";
    }

    FlowNode* duplicate(bool recursive, int use_count, bool duplicate_leaf) override {
        FlowNodeDummy* fh = new FlowNodeDummy();
        fh->duplicate_internal(this,recursive,use_count, duplicate_leaf);
        return fh;
    }

    FlowNodeDummy() {
        _find = &find_ptr;
    }
    FLOW_NODE_DEFINE(FlowNodeDummy,find_dummy);

    void release_child(FlowNodePtr child, FlowNodeData data) override {
        click_chatter("TODO : release child of flow node dummy");
        assert(false);
    }

    FlowNodePtr* find_dummy(FlowNodeData data, bool&) {
        return &_default;
    }

    virtual ~FlowNodeDummy() {
    }

    NodeIterator iterator() override {
        return NodeIterator(new DummyIterator(this));
    }
};

/**
 * Node supporting only one value and a default value
 */
class FlowNodeTwoCase : public FlowNode  {
    FlowNodePtr child;

    class TwoCaseIterator : public NodeIteratorBase{
        FlowNodeTwoCase* _node;
        int cur;
    public:
        TwoCaseIterator(FlowNodeTwoCase* n) : cur(0) {
            _node = n;
        }

        FlowNodePtr* next() {
            if (cur++ == 0)
                return &_node->child;
            else
                return 0;
        }
    };

    public:
    String name() const {
        return "TWOCASE";
    }

    FlowNode* duplicate(bool recursive, int use_count, bool duplicate_leaf) override {
        FlowNodeTwoCase* fh = new FlowNodeTwoCase(child);
        fh->duplicate_internal(this,recursive,use_count, duplicate_leaf);
        return fh;
    }

    FlowNodeTwoCase(FlowNodePtr c) : child(c) {
        c.set_parent(this);
        _find = &find_ptr;
    }
    FLOW_NODE_DEFINE(FlowNodeTwoCase,find_two);

    FlowNodePtr* find_two(FlowNodeData data, bool&) {
        if (data.equals(child.data()))
            return &child;
        else
            return &_default;
    }

    void release_child(FlowNodePtr child, FlowNodeData data) override {
        click_chatter("TODO : release two case");
        /*if (child_deletable()) {
			if (is_leaf()) {
				children[get_idx(child.data())].ptr = NULL;
				num--;
			} else {
				child.node->release();;
				num--;
			}
		}*/
        assert(false);
    }

    virtual ~FlowNodeTwoCase() {
        if (child.ptr && child.is_node())
            child.node->destroy();
    }

    NodeIterator iterator() override {
        return NodeIterator(new TwoCaseIterator(this));
    }
};

/**
 * Node supporting only one value and a default value
 */
class FlowNodeThreeCase : public FlowNode  {
    FlowNodePtr childA;
    FlowNodePtr childB;

    class ThreeCaseIterator : public NodeIteratorBase {
        FlowNodeThreeCase* _node;
        int cur;
    public:
        ThreeCaseIterator(FlowNodeThreeCase* n) : cur(0) {
            _node = n;
        }

        FlowNodePtr* next() {

            FlowNodePtr* ptr = 0;
            if (cur == 0)
                ptr = &_node->childA;
            else if (cur == 1)
                ptr = &_node->childB;
            else
                ptr = 0;
            cur ++;
            return ptr;
        }
    };

    public:
    String name() const {
        return "THREECASE";
    }

    FlowNode* duplicate(bool recursive, int use_count, bool duplicate_leaf) override {
        FlowNodeThreeCase* fh = new FlowNodeThreeCase(childA,childB);
        fh->duplicate_internal(this,recursive,use_count, duplicate_leaf);
        return fh;
    }

    FlowNodeThreeCase(FlowNodePtr a,FlowNodePtr b) : childA(a),childB(b) {
        a.set_parent(this);
        b.set_parent(this);
        _find = &find_ptr;
    }
    FLOW_NODE_DEFINE(FlowNodeThreeCase,find_three);

    FlowNodePtr* find_three(FlowNodeData data, bool&) {
        if (data.equals(childA.data()))
            return &childA;
        else if (data.equals(childB.data()))
            return &childB;
        else
            return &_default;
    }

    void release_child(FlowNodePtr child, FlowNodeData data) override {
        click_chatter("TODO renew release threecase");
        /*if (child_deletable()) {
			if (is_leaf()) {
				children[get_idx(child.data())].ptr = NULL;
				num--;
			} else {
				child.node->release();;
				num--;
			}
		}*/
        assert(false);
    }

    virtual ~FlowNodeThreeCase() {
        if (childA.ptr && childA.is_node())
            childA.node->destroy();
        if (childB.ptr && childB.is_node())
            childB.node->destroy();
    }

    NodeIterator iterator() override {
        return NodeIterator(new ThreeCaseIterator(this));
    }
};

/**
 * Flow to be replaced by optimizer containing multiple supplementary informations
 *  not to be used at runtime !
 */
class FlowNodeDefinition : public FlowNodeHash<HASH_SIZES_NR - 1> { public:
    bool _else_drop;
    String _hint;
    Element* _creator;

    FlowNodeDefinition(Element* creator) : _else_drop(false), _hint(String::make_empty()), _creator(creator) {

    }

    String name() const {
        return String("DEFINITION") + (_else_drop?"!":"");
    }

    FlowNodeDefinition* duplicate(bool recursive,int use_count, bool duplicate_leaf) override;

    FlowNode* create_final(Bitvector threads);
};

inline void FlowNodePtr::traverse_all_leaves(std::function<void(FlowNodePtr*)> fnt) {
    if (is_leaf()) {
        fnt(this);
    } else {
        node->traverse_all_leaves(fnt,true,true);
    }
}

inline void FlowNodePtr::check() {
    if (!is_leaf())
        node->check();
}


#define POOL_SZ 4096
#define EXCH_MAX 1024

/**
 * FlowAllocator
 */
template<class T>
class FlowAllocator { public:
    static per_thread<pool_allocator_aware_mt<T,POOL_SZ, EXCH_MAX> >& instance() {
        static per_thread<pool_allocator_aware_mt<T,POOL_SZ, EXCH_MAX> > instance;
        flow_assert(instance.weight() == click_max_cpu_ids());
        flow_assert(instance->initialized());
        return instance;
    }

    static T* allocate() {
        T* v = instance()->allocate_uninitialized();
        flow_assert(v->_default.ptr == 0);
        flow_assert(!v->growing());
        flow_assert(v->num == 0);
        flow_assert(v->_find);

#if FLOW_KEEP_STRUCTURE
        flow_assert(!v->released());
#endif
        return v;
    }

    static void release(T* e) {
        flow_assert(pool_allocator_mt_base::dying() || e->num == 0);
        flow_assert(!e->growing());
        instance()->release_unitialized(e);
    }
};

#endif
