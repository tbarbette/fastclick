#ifndef CLICK_FLOW_NODES_HH
#define CLICK_FLOW_NODES_HH 1

#include <click/allocator.hh>
#include <click/straccum.hh>

#define FLOW_LEVEL_DEFINE(T,fnt) \
        static FlowNodeData get_data_ptr(void* thunk, Packet* p) {\
            return static_cast<T*>(thunk)->fnt(p);\
        }

class FlowNode;

class FlowNodePtr {
private:
    bool _is_leaf;
public :
    union {
        FlowNode* node;
        FlowControlBlock* leaf;
        void* ptr;
    };

    FlowNodePtr() : _is_leaf(false), ptr(0) {

    }

    FlowNodePtr(FlowNode* n) : _is_leaf(false), node(n) {

    }

    FlowNodePtr(FlowControlBlock* sfcb) : _is_leaf(true), leaf(sfcb) {

    }

    inline FlowNodeData data();

    inline FlowNode* parent() const;

    inline void set_data(FlowNodeData data);

    inline void set_parent(FlowNode* parent);

    inline void set_leaf(FlowControlBlock* l) {
        leaf = l;
        _is_leaf = true;
    }

    inline void set_node(FlowNode* n) {
        node = n;
        _is_leaf = false;
    }


    inline bool is_leaf() const {
        return _is_leaf;
    }

    inline bool is_node() const {
        return !_is_leaf;
    }

    /*  inline FlowNodePtr duplicate() {
        FlowNodePtr v;
        v.ptr = ptr;
        v._is_leaf = _is_leaf;
        v.set_data(data());
        return v;
    }
     */
    void print() const;

    //---
    //Compile time functions
    //---

    FlowNodePtr optimize(bool mt_safe);

    bool else_drop();

    inline void traverse_all_leaves(std::function<void(FlowNodePtr*)>);

    inline void check();

    bool replace_leaf_with_node(FlowNode*, bool discard);

    void node_combine_ptr(FlowNode* parent, FlowNodePtr, bool as_child, bool priority);
    void default_combine(FlowNode* parent, FlowNodePtr*, bool as_child, bool priority);
};


class FlowLevel {
private:
    //bool deletable;
protected:
    bool _dynamic;
    bool _islong = false;
public:
    FlowLevel() :
        //deletable(true),
        _dynamic(false) {

    };

    typedef FlowNodeData (*GetDataFn)(void*, Packet* packet);
    GetDataFn _get_data;

    virtual ~FlowLevel() {};
    virtual long unsigned get_max_value() = 0;

    virtual void add_offset(int offset) {};

    virtual bool is_mt_safe() const { return false;};

    virtual bool is_usefull() {
        return true;
    }
    /**
     * Prune this level with another level, that is we know that this level
     *  is a sub-path of the other one, and there is no need to classify on
     *  the given level
     */
    virtual void prune(FlowLevel*) {};

    virtual FlowNodePtr prune(FlowLevel* other, FlowNodeData data, FlowNode* node, bool &changed) {
        return FlowNodePtr(node);
    }

    /**
     * Tell if two node are of the same type, and on the same field/value/mask if applicable
     *
     * However this is not checking if runtime data and dynamic are equals
     */
    virtual bool equals(FlowLevel* level) {
        return typeid(*this) == typeid(*level) && _islong == level->_islong && _dynamic == level->_dynamic;
    }

    inline FlowNodeData get_data(Packet* p) {
        return _get_data(this,p);
    }

    int current_level = 0;

    FlowNode* create_node(FlowNode* parent, bool better, bool better_impl);

    bool is_dynamic() {
        return _dynamic;
    }

    void set_dynamic() {
        _dynamic = true;
    }

    /*bool is_deletable() {
        return deletable;
    }*/

    virtual String print() = 0;

    inline bool is_long() const {
        return _islong;
    }

    FlowLevel* assign(FlowLevel* l) {
        _dynamic = l->_dynamic;
        //deletable = l->deletable;
        _islong = l->_islong;
        return this;
    }

    virtual FlowLevel *duplicate() = 0;

    virtual FlowLevel *optimize() {
        return this;
    }
};

#define FLOW_NODE_DEFINE(T,fnt) \
        static FlowNodePtr* find_ptr(void* thunk, FlowNodeData data) {\
            return static_cast<T*>(thunk)->fnt(data);\
        }


class FlowNode {
private:
    static void print(const FlowNode* node,String prefix,int data_offset = -1, bool show_ptr = true);

protected:
    int num;
    FlowLevel* _level;
    FlowNodePtr _default;
    FlowNode* _parent;

    //bool _child_deletable;
    bool _released;
    bool _growing;
    //FlowNodePtr*(_find)(FlowNode*,FlowNodeData data);

    typedef FlowNodePtr* (*FindFn)(void*,FlowNodeData data);

    FindFn _find;

    void duplicate_internal(FlowNode* node, bool recursive, int use_count) {
        assign(node);
        if (unlikely(recursive)) {
            this->_level = node->level()->duplicate();
            if (_default.ptr) {
                if ( _default.is_node()) {
                    _default.set_node(node->_default.node->duplicate(recursive, use_count));
                } else {
                    _default.set_leaf(_default.leaf->duplicate(use_count));
                }
                _default.set_parent(this);
            }

            NodeIterator it = node->iterator();
            FlowNodePtr* child;
            while ((child = it.next()) != 0) {
                if (child->is_leaf()) {
                    FlowControlBlock* new_leaf = child->leaf->duplicate(use_count);
                    new_leaf->parent = this;
                    add_leaf(child->data(),new_leaf);
                } else {
                    FlowNode* new_node = child->node->duplicate(recursive, use_count);
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
     * True if matches
     */
    inline bool _leaf_reverse_match(FlowControlBlock* &leaf, Packet* &p) {
        if (default_ptr()->ptr == leaf) { //If default, we need to check it does not match any non-default
#if DEBUG_CLASSIFIER_MATCH > 1
            click_chatter("%d -> %p %p",level()->get_data(p),this->find_or_default(level()->get_data(p))->ptr,leaf);
#endif
            return this->find_or_default(level()->get_data(p))->ptr == leaf;
        } else
            return level()->get_data(p).data_64 == leaf->data_64[0];
    }

    inline bool _node_reverse_match(FlowNode* &child, Packet* &p) {
        if (default_ptr()->ptr == child) //If default, we need to check it does not match any non-default
            return this->find_or_default(level()->get_data(p))->ptr == child;
        else
            return level()->get_data(p).data_64 == child->node_data.data_64;
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


    virtual void destroy() {
        delete this;
    }


    FlowNode() :  num(0),_level(0),_default(),_parent(0), _growing(false),
//            _child_deletable(true),
            _released(false) {
        node_data.data_64 = 0;
    }

    static FlowNode* create_hash(int l);
    FlowLevel* level() const {
        return _level;
    }
    inline void add_node(FlowNodeData data, FlowNode* node) {
        FlowNodePtr* ptr = _find(this,data);
        assert(ptr->ptr == 0);
        inc_num();
        ptr->set_node(node);
        ptr->set_data(data);
    }

    inline void add_leaf(FlowNodeData data, FlowControlBlock* leaf) {
        FlowNodePtr* ptr = _find(this,data);
        assert(ptr->ptr == 0);
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

    FlowNode* combine(FlowNode* other, bool as_child, bool priority) CLICK_WARN_UNUSED_RESULT;
    void __combine_child(FlowNode* other, bool priority);
    void __combine_else(FlowNode* other, bool priority);
    FlowNodePtr prune(FlowLevel* level,FlowNodeData data, bool inverted, bool &changed) CLICK_WARN_UNUSED_RESULT;


    virtual int max_size() const {
        return INT_MAX;
    }
    virtual FlowNode* duplicate(bool recursive,int use_count) = 0;

    void assign(FlowNode* node) {
        _level = node->_level;
        _parent = node->_parent;
        _default = node->_default;
    }

    int getNum() const { return num; };

    //To use for testing purposes only
    int findGetNum();

    virtual ~FlowNode() {
        if (_default.ptr && _default.is_node())
            _default.node->release();
    };

    inline bool released() const {
        return _released;
    }

    inline void release() {
        flow_assert(!growing());
        _released = true;
#if DEBUG_CLASSIFIER
        apply([](FlowNodePtr* p) {
            if (p->ptr && p->is_node()) {
                flow_assert(p->node->released());
            }
        });
#endif
    };

    inline bool child_deletable() const {
        //return _child_deletable;
        return true;
    }


    inline FlowNodePtr* find(FlowNodeData data) {
        return _find((void*)this,data);
    }


    inline FlowNodePtr* find_or_default(const FlowNodeData data) {
        FlowNodePtr* ptr = _find(this,data);
        if (ptr->ptr == 0 || (ptr->is_node() && ptr->node->released()))
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
            assert(ptr.ptr != this);
            _default = ptr;
            _default.set_parent(this);
        }
        assert(_default.ptr == ptr.ptr);
    }


    inline void set_default(FlowNode* node) {
        assert(_default.ptr == 0);
        assert(node != this);
        _default.ptr = node;
        node->set_parent(this);
    }

    virtual void release_child(FlowNodePtr fl, FlowNodeData data) = 0;

    inline void renew() {
        _released = false;
    }

    virtual String name() const = 0;

    class NodeIteratorBase {
    public:
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
        assert(g); //There is no stopping of growing, when it stops, the table should be deleted
        _growing = g;
    }
    FlowNode* start_growing(bool impl);

#if DEBUG_CLASSIFIER || DEBUG_CLASSIFIER_CHECK
    void check(bool allow_parent = false);
#else
    inline void check(bool allow_parent = false) {};
#endif

    virtual FlowNode* optimize(bool mt_safe) CLICK_WARN_UNUSED_RESULT;

    void traverse_all_leaves(std::function<void(FlowNodePtr*)> fnt, bool do_final, bool do_default);

    void traverse_all_leaves_and_empty_default(std::function<void(FlowNodePtr*,FlowNode*)> fnt, bool do_final, bool do_default);

    bool traverse_all_default_leaf(std::function<bool(FlowNode*)> fnt);

    bool traverse_all_nodes(std::function<bool(FlowNode*)> fnt);

    void traverse_parents(std::function<bool(FlowNode*)> fnt);

    void traverse_parents(std::function<void(FlowNode*)> fnt);

    bool is_dummy();

    bool is_full_dummy();

    bool has_no_default(bool allow_dynamic = false);


    inline void print(int data_offset = -1, bool show_ptr = true) const {
        click_chatter("---");
        print(this,"", data_offset, show_ptr);
        click_chatter("---");
    }
#if DEBUG_CLASSIFIER
    void debug_print(int data_offset = -1) const {
        print(data_offset,true);
    }
#else
    void debug_print(int = -1) const {}
#endif
private:
    void leaf_combine_data(FlowControlBlock* leaf, bool do_final, bool do_default);
    void leaf_combine_data_create(FlowControlBlock* leaf, bool do_final, bool do_default, bool discard_my_data);
    FlowNodePtr prune(FlowNode* parent, FlowNodeData data);
    FlowNodePtr prune_with_parent(FlowNode* parent, FlowNodeData data, bool was_default);
    FlowNode* replace_leaves(FlowNode* other, bool do_final, bool do_default, bool discard_my_fcb_data);
    friend class FlowClassificationTable;
    friend class FlowNodePtr;
    friend class FlowNodeDefinition;
};

/**
 * Dummy FlowLevel used for the default path before merging a table
 */
class FlowLevelDummy  : public FlowLevel {
public:

    FlowLevelDummy() {
        _get_data = &get_data_ptr;
    }
    FLOW_LEVEL_DEFINE(FlowLevelDummy,get_data_dummy);

    inline long unsigned get_max_value() {
        return 0;
    }

    inline FlowNodeData get_data_dummy(Packet* packet) {
        click_chatter("FlowLevelDummy should be stripped !");
        abort();
    }

    String print() {
        return String("ANY");
    }

    FlowLevel* duplicate() override {
        return (new FlowLevelDummy())->assign(this);
    }
};

/**
 * FlowLevel based on the aggregate
 */
class FlowLevelAggregate  : public FlowLevel {
public:

    FlowLevelAggregate(int offset, uint32_t mask) : offset(offset), mask(mask) {
        _get_data = &get_data_ptr;
    }

    FlowLevelAggregate() : FlowLevelAggregate(0,-1) {
        _get_data = &get_data_ptr;
    }
    FLOW_LEVEL_DEFINE(FlowLevelAggregate,get_data_agg);


    int offset;
    uint32_t mask;

    inline long unsigned get_max_value() {
        return mask;
    }

    inline FlowNodeData get_data_agg(Packet* packet) {
        FlowNodeData data;
        data.data_32 = (AGGREGATE_ANNO(packet) >> offset) & mask;
        return data;
    }

    String print() {
        return String("AGG");
    }

    FlowLevel* duplicate() override {
        return (new FlowLevelAggregate(0,-1))->assign(this);
    }
};

/**
 * FlowLevel based on the current thread
 */
class FlowLevelThread  : public FlowLevel {
    int _numthreads;

public:
    FlowLevelThread(int nthreads) : _numthreads(nthreads) {
        _get_data = &get_data_ptr;
        _dynamic = true;
    }
    FLOW_LEVEL_DEFINE(FlowLevelThread,get_data_thread);

    virtual bool is_mt_safe() const override { return true;};
    inline long unsigned get_max_value() {
        return _numthreads;
    }

    inline FlowNodeData get_data_thread(Packet*) {
        return (FlowNodeData){.data_8 = (uint8_t)click_current_cpu_id()};
    }

    String print() {
        return String("THREAD");
    }

    FlowLevel* duplicate() override {
        return (new FlowLevelThread(_numthreads))->assign(this);
    }
};

class FlowLevelOffset : public FlowLevel {
protected:
    int _offset;

public:

    FlowLevelOffset(int offset) :  _offset(offset) {

    }
    FlowLevelOffset() :  FlowLevelOffset(0) {
    }


    void add_offset(int offset) {
        _offset += offset;
    }

    int offset() const {
        return _offset;
    }

    virtual int mask_size() const = 0;

    virtual uint8_t get_mask(int o) const = 0;

    bool equals(FlowLevel* level) {
        return ((FlowLevel::equals(level))&& (_offset == dynamic_cast<FlowLevelOffset*>(level)->_offset));
    }

};

static const char hex[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','F'};

/**
 * Flow level for any offset/mask of T bits
 */
template <typename T>
class FlowLevelGeneric : public FlowLevelOffset {
private:
    T _mask;
public:

    FlowLevelGeneric(T mask, int offset) : _mask(mask), FlowLevelOffset(offset) {
        _get_data = &get_data_ptr;
    }
    FLOW_LEVEL_DEFINE(FlowLevelGeneric<T>,get_data);

    FlowLevelGeneric() : FlowLevelGeneric(0,0) {

    }
    void set_match(int offset, T mask) {
        _mask = mask;
        _offset = offset;
    }

    T mask() const {
        return _mask;
    }

    virtual int mask_size() const {
        return sizeof(T);
    }

    virtual uint8_t get_mask(int o) const {
        if (o < _offset)
            return 0;
        if (o > _offset + mask_size())
            return 0;
        return ((uint8_t*)&_mask)[o-_offset];
    }

    virtual bool is_usefull() override {
        return _mask != 0;
    }

    //Remove from the mask what is already set in other
    virtual void prune(FlowLevel* other) override;

    /**
     * Remove children that don't match data at offset overlaps
     */
    virtual FlowNodePtr prune(FlowLevel* other, FlowNodeData data, FlowNode* node, bool &changed) override;

    inline long unsigned get_max_value() {
        return _mask;
    }

    inline FlowNodeData get_data(Packet* packet) {
        return FlowNodeData((T)(*((T*)(packet->data() + _offset)) & _mask));
    }

    String print() {
        StringAccum s;
        s << _offset;
        s << "/";
        for (int i = 0; i < sizeof(T); i++) {
            uint8_t t = ((uint8_t*)&_mask)[i];
            s << hex[t >> 4] << hex[t & 0xf];
        }
        return s.take_string();
    }

    FlowLevel* duplicate() override {
        return (new FlowLevelGeneric<T>(_mask,_offset))->assign(this);
    }

    bool equals(FlowLevel* level) {
        return ((FlowLevelOffset::equals(level)) && (_mask == dynamic_cast<FlowLevelGeneric<T>*>(level)->_mask));
    }


    virtual FlowLevel *optimize() override;
};

/**
 * Flow level for any offset/mask of T bits
 */
template <typename T>
class FlowLevelField : public FlowLevelOffset {
public:

    FlowLevelField(int offset) : FlowLevelOffset(offset) {
        _get_data = &get_data_ptr;
    }
    FLOW_LEVEL_DEFINE(FlowLevelField<T>,get_data);

    FlowLevelField() : FlowLevelField(0) {

    }

    T mask() const {
        return (T)-1;
    }

    virtual uint8_t get_mask(int o) const {
        if (o < _offset)
            return 0;
        if (o > _offset + mask_size())
            return 0;
        return 0xff;
    }

    virtual int mask_size() const {
        return sizeof(T);
    }

    inline long unsigned get_max_value() {
        return mask();
    }

    inline FlowNodeData get_data(Packet* packet) {
        //click_chatter("%d %x",_offset,*(T*)(packet->data() + _offset));
        return FlowNodeData(*(T*)(packet->data() + _offset));
    }

    String print() {
        StringAccum s;
        s << _offset;
        s << "/";
        for (int i = 0; i < sizeof(T); i++) {
            s << "FF";
        }
        return s.take_string();
    }

    FlowLevel* duplicate() override {
        return (new FlowLevelField<T>(_offset))->assign(this);
    }

};

using FlowLevelGeneric8 = FlowLevelGeneric<uint8_t>;
using FlowLevelGeneric16 = FlowLevelGeneric<uint16_t>;
using FlowLevelGeneric32 = FlowLevelGeneric<uint32_t>;
using FlowLevelGeneric64 = FlowLevelGeneric<uint64_t>;

using FlowLevelField8 = FlowLevelField<uint8_t>;
using FlowLevelField16 = FlowLevelField<uint16_t>;
using FlowLevelField32 = FlowLevelField<uint32_t>;
using FlowLevelField64 = FlowLevelField<uint64_t>;

/**
 * Node implemented using a linkedlist
 *//*
class FlowNodeList : public FlowNode {
	std::list<FlowNode*> childs;
	int highwater = 16;
public:

	FlowNodeList() {};

	FlowNode* find(FlowNodeData data) {

		for (auto it = childs.begin();it != childs.end(); it++)
			if ((*it)->data == data)
				return *it;
		return NULL;
	}

	void set(uint32_t data, FlowNode* fl) {
		childs.push_back(fl);
		num++;
		if (_child_deletable && num > highwater) {

			int freed = 0;
			for (auto it = childs.begin();it != childs.end(); it++) {

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
		childs.remove(child);
		num--;
	}

	virtual void release_child(FlowNode* child, FlowNodeData data) {
		_remove(child);
	}

	virtual ~FlowNodeList() {
		//click_chatter("Delete list");
		for (auto it = childs.begin(); it != childs.end(); it++)
			delete (*it);
	}
};*/

/**
 * Node implemented using an array
 */
class FlowNodeArray : public FlowNode  {
    Vector<FlowNodePtr> childs;

public:
    void destroy() override;

    FlowNodeArray() {
        _find = &find_ptr;
    }

    void initialize(unsigned int max_size) {
        childs.resize(max_size);
    }

    FLOW_NODE_DEFINE(FlowNodeArray,find_array);

    inline FlowNodePtr* find_array(FlowNodeData data) {
        return &childs[data.data_32];
    }


    String name() const {
        return "ARRAY";
    }
    /*
	void _remove(FlowNode* child) {
		delete childs[child->data];
		childs[child->data] = NULL;
		num--;
	}*/

    void release_child(FlowNodePtr child, FlowNodeData data) {
        if (child_deletable()) {
            if (child.is_leaf()) {
                childs[data.data_32].ptr = 0; //FCB deletion is handled by the caller which goes bottom up
            } else {
                if (growing()) {
                    child.node->destroy();
                    child.node = 0;
                } else
                    child.node->release();
            }
            num--;
        }
    }

    ~FlowNodeArray();

    FlowNode* duplicate(bool recursive,int use_count) override;

    class ArrayNodeIterator : public NodeIteratorBase {
        FlowNodeArray* _node;
        int cur;
    public:
        ArrayNodeIterator(FlowNodeArray* n) : cur(0) {
            _node = n;
        }

        FlowNodePtr* next() {
            do {
                if (cur >= _node->childs.size())
                    return 0;
                if (_node->childs[cur].ptr)
                    return &_node->childs[cur++];
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

    #define DESTRUCTED_NODE (void*)-1

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

    FlowNodePtr childs[capacity()];

    //uint8_t capacity_n; //Index into the hash_sizes array
//    uint32_t hash_size;
//    uint32_t mask;
//    uint32_t highwater;
//    uint32_t max_highwater;
    /*inline uint32_t highwater() const {
        return capacity_n / 3;
    }*/
    inline constexpr uint32_t max_highwater() const {
        return 3 * (capacity() / 5);
    }

    inline constexpr uint32_t collision_threshold() const {
        return ((capacity() / 20) > 32 ? 32 : capacity()/20);
    }

    inline constexpr uint32_t hole_threshold() const {
        return ((capacity() / 30) > 24 ? 24 : capacity()/30);
    }

    virtual int max_size() const override {
        return max_highwater();
    }
    unsigned int hash32(uint32_t d) const {
        return ((d << 3) + (d >> 9) + (d >> 12) + (d >> 25)) % capacity();
    }

    unsigned int hash64(uint64_t ld) const {
        uint32_t d = (uint32_t)ld ^ (ld >> 32);
        return ((d << 3) + (d >> 9) + (d >> 12) + (d >> 25)) % capacity();
    }


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
        int cur;
    public:
        HashNodeIterator(FlowNodeHash* n) : cur(0) {
            _node = n;
        }

        virtual FlowNodePtr* next() override {
            while (cur < _node->capacity() && (_node->childs[cur].ptr == 0 || _node->childs[cur].ptr == DESTRUCTED_NODE)) cur++;
            if (cur >= _node->capacity())
                return 0;
            return &_node->childs[cur++];
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

		Vector<FlowNodePtr> childs_old = childs;
		childs.resize(hash_size);
		num = 0;

		for (int i = 0; i < current_size; i++) {
			childs[i].ptr= NULL;
		}
		size_n++;
		for (int i = 0; i < current_size; i++) {
			if (childs_old[i].ptr) {
				if (childs_old[i].is_leaf()) {
				GET IDX CANNOT BE USED FOR OLD
					childs[get_idx(childs_old[i].data())] = childs_old[i];
				} else {
					if (childs_old[i].node->released()) {
						delete childs_old[i].node;
					} else {
						childs[get_idx(childs_old[i].data())] = childs_old[i];
					}
				}
			}
		}*/
    }

    public:
    FlowNodeHash() {
        //hash_size = hash_sizes[size_n];
        //mask = hash_size - 1;
        //highwater = hash_size / 3;
        //max_highwater = hash_size / 2;
        //childs.resize(hash_size);
        _find = &find_ptr;
#if DEBUG_CLASSIFIER
        for (int i = 0; i < capacity(); i++) {
            assert(childs[i].ptr == NULL);
        }
#endif
    };
    virtual FlowNode* duplicate(bool recursive,int use_count) override;

    FLOW_NODE_DEFINE(FlowNodeHash,find_hash);

    static uint32_t capacity_for(int level) {
        return level << (8+level);
    }

    //virtual void renew() override;
    FlowNodePtr* find_hash(FlowNodeData data);

    void release_child(FlowNodePtr child, FlowNodeData data);

    virtual ~FlowNodeHash() {
        //Same than destroy but don't release ourselves at the end
        for (int i = 0; i < capacity(); i++) {
            if (childs[i].ptr && childs[i].ptr != DESTRUCTED_NODE && !childs[i].is_leaf()) {
                childs[i].node->destroy();
                childs[i].node = 0;
            }
        }
        static_assert(capacity() < INT_MAX / 2);
        static_assert(step() < capacity() / 2);
    }

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
        int cur;
    public:
        DummyIterator(FlowNode* n) : cur(0) {
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

    int max_size() const {
        return 0;
    }

    FlowNode* duplicate(bool recursive,int use_count) override {
        FlowNodeDummy* fh = new FlowNodeDummy();
        fh->duplicate_internal(this,recursive,use_count);
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

    FlowNodePtr* find_dummy(FlowNodeData data) {
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

    FlowNode* duplicate(bool recursive,int use_count) override {
        FlowNodeTwoCase* fh = new FlowNodeTwoCase(child);
        fh->duplicate_internal(this,recursive,use_count);
        return fh;
    }

    int max_size() const {
        return 1;
    }

    FlowNodeTwoCase(FlowNodePtr c) : child(c) {
        c.set_parent(this);
        _find = &find_ptr;
    }
    FLOW_NODE_DEFINE(FlowNodeTwoCase,find_two);

    FlowNodePtr* find_two(FlowNodeData data) {
        if (data.data_64 == child.data().data_64)
            return &child;
        else
            return &_default;
    }

    void release_child(FlowNodePtr child, FlowNodeData data) override {
        click_chatter("TODO : release two case");
        /*if (child_deletable()) {
			if (is_leaf()) {
				childs[get_idx(child.data())].ptr = NULL;
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
    int max_size() const {
        return 2;
    }

    FlowNode* duplicate(bool recursive,int use_count) override {
        FlowNodeThreeCase* fh = new FlowNodeThreeCase(childA,childB);
        fh->duplicate_internal(this,recursive,use_count);
        return fh;
    }

    FlowNodeThreeCase(FlowNodePtr a,FlowNodePtr b) : childA(a),childB(b) {
        a.set_parent(this);
        b.set_parent(this);
        _find = &find_ptr;
    }
    FLOW_NODE_DEFINE(FlowNodeThreeCase,find_three);

    FlowNodePtr* find_three(FlowNodeData data) {
        if (data.data_64 == childA.data().data_64)
            return &childA;
        else if (data.data_64 == childB.data().data_64)
            return &childB;
        else
            return &_default;
    }

    void release_child(FlowNodePtr child, FlowNodeData data) {
        click_chatter("TODO renew release threecase");
        /*if (child_deletable()) {
			if (is_leaf()) {
				childs[get_idx(child.data())].ptr = NULL;
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

    FlowNodeDefinition() : _else_drop(false), _hint(String::make_empty()) {

    }

    String name() const {
        return String("DEFINITION") + (_else_drop?"!":"");
    }

    FlowNodeDefinition* duplicate(bool recursive,int use_count) override;

    FlowNode* create_final(bool mt_safe);
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
    static per_thread<pool_allocator_mt<T,true,POOL_SZ, EXCH_MAX> >& instance() {
        static per_thread<pool_allocator_mt<T,true,POOL_SZ, EXCH_MAX> > instance;
        return instance;
    }
    static T* allocate() {
        return instance()->allocate();
    }
    static void release(T* e) {
        instance()->release(e);
    }
};

//template<int capacity> class FlowAllocator<FlowNodeHash<capacity> > : public FlowAllocator {};



#endif
