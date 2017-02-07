#ifndef CLICK_FLOW_HH
#define CLICK_FLOW_HH 1

#include <click/timestamp.hh>
#include <click/error.hh>
#include <click/sync.hh>
#include <click/flow_common.hh>
#include <click/packet.hh>
#include <click/multithread.hh>
#include <click/bitvector.hh>
#include <click/vector.hh>
#include <click/list.hh>
//#include <openflow/openflow.h>
#include <thread>
#include <assert.h>

CLICK_DECLS

#ifdef HAVE_FLOW

#define DEBUG_CLASSIFIER_MATCH 0 //0 no, 1 build-time only, 2 whole time, 3 print table after each dup
#define DEBUG_CLASSIFIER_RELEASE 3
#define DEBUG_CLASSIFIER 0 //1 : Build-time only, >1 : whole time

typedef void (*sfcb_combiner)(FlowControlBlock* old_sfcb, FlowControlBlock* new_sfcb);

class FlowLevel {
protected:
    bool deletable;
    bool _dynamic;
    bool _islong = false;
public:
    FlowLevel() : deletable(true),_dynamic(false) {};



    virtual ~FlowLevel() {};
    virtual long unsigned get_max_value() = 0;
    virtual FlowNodeData get_data(Packet* packet) = 0;
    virtual void add_offset(int offset) {};

    virtual bool equals(FlowLevel* level) {
        return typeid(*this) == typeid(*level);
    }




    bool is_dynamic() {
        return _dynamic;
    }

    void set_dynamic() {
        _dynamic = true;
    }

    bool is_deletable() {
        return deletable;
    }

    virtual String print() = 0;

    inline bool is_long() const {
        return _islong;
    }

    FlowLevel* assign(FlowLevel* l) {
        _dynamic = l->_dynamic;
        deletable = l->deletable;
        _islong = l->_islong;
        return this;
    }

    virtual FlowLevel *duplicate() = 0;
};

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

    inline FlowNode* parent();

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


    inline bool is_leaf() {
        return _is_leaf;
    }

    inline bool is_node() {
        return !_is_leaf;
    }

    /*	inline FlowNodePtr duplicate() {
		FlowNodePtr v;
		v.ptr = ptr;
		v._is_leaf = _is_leaf;
		v.set_data(data());
		return v;
	}
     */
    void print();

};

class FlowNode {
private:
    static void print(FlowNode* node,String prefix);

protected:
    int num;
    FlowLevel* _level;
    FlowNodePtr _default;
    FlowNode* _parent;
    bool _child_deletable;
    bool _released;

    void duplicate_internal(FlowNode* node, bool recursive, int use_count) {
        assign(node);
        if (unlikely(recursive)) {
            this->_level = node->level()->duplicate();
            if (_default.ptr) {
                if ( _default.is_node()) {
                    _default.set_node(node->_default.node->duplicate(recursive, use_count));
                } else {
                    _default.set_leaf(_default.leaf->duplicate(1));
                }
                _default.set_parent(this);
            }

            NodeIterator* it = node->iterator();
            FlowNodePtr* child;
            while ((child = it->next()) != 0) {
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
        FlowNode::NodeIterator* it = this->iterator();
        FlowNode::NodeIterator* ito = node->iterator();
        FlowNodePtr* child;
        FlowNodePtr* childo;
        while ((child = it->next()) != 0) {
            childo = ito->next();
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

    FlowNode() :  num(0),_level(0),_default(),_parent(0),_child_deletable(true),_released(false) {
        node_data.data_64 = 0;
    }

    FlowLevel* level() {
        return _level;
    }

    inline void add_node(FlowNodeData data, FlowNode* node) {
        FlowNodePtr* ptr = find(data);
        if (ptr->ptr == 0)
            inc_num();
        ptr->set_node(node);
        ptr->set_data(data);
    }

    inline void add_leaf(FlowNodeData data, FlowControlBlock* leaf) {
        FlowNodePtr* ptr = find(data);
        if (ptr->ptr == 0)
            inc_num();
        ptr->set_leaf(leaf);
        ptr->set_data(data);
    }

    FlowNode* combine(FlowNode* other) CLICK_WARN_UNUSED_RESULT;

    virtual FlowNode* duplicate(bool recursive,int use_count) = 0;

    void assign(FlowNode* node) {
        _level = node->_level;
        _parent = node->_parent;
        _default = node->_default;
    }

    int getNum() { return num; };

    virtual ~FlowNode() {};

    inline bool released() const {
        return _released;
    }

    inline void release() {
        _released = true;
    }

    inline bool child_deletable() {
        return _child_deletable;
    }

    /**
     * Find the child node corresponding to the given data but do not create it
     */
    virtual FlowNodePtr* find(const FlowNodeData data) = 0;

    FlowNodePtr* find_or_default(const FlowNodeData data) {
        FlowNodePtr* ptr = find(data);
        if (ptr->ptr == 0)
            return default_ptr();
        return ptr;
    }

    virtual void inc_num() {
        num++;
    }

    virtual void dec_num() {
        num--;
    }

    inline FlowNode* parent() {
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
            click_chatter("Changing default of %p to %p",_default.ptr);
        } else {
            click_chatter("No default");
        }
        assert(_default.ptr == ptr.ptr);
    }


    inline void set_default(FlowNode* node) {
        assert(_default.ptr == 0);
        assert(node != this);
        _default.ptr = node;
        node->set_parent(this);
    }

    virtual void release_child(FlowNodePtr fl) = 0;

    virtual void renew() {
        _released = false;
    }

    virtual String name() = 0;

    class NodeIterator {
    public:
        virtual FlowNodePtr* next() = 0;
    };

    virtual NodeIterator* iterator() = 0;

    inline void set_parent(FlowNode* parent) {
        _parent = parent;
    }

#if DEBUG_CLASSIFIER
    void check();
#else
    inline void check() {};
#endif

    static inline FlowNode* create(FlowNode* parent, FlowLevel* level);

    virtual FlowNode* optimize() CLICK_WARN_UNUSED_RESULT;

    FlowNodePtr* get_first_leaf_ptr();

    class LeafIterator {
    private:
        typedef struct SfcbItem_t {
            FlowControlBlock* sfcb;
            struct SfcbItem_t* next;
        } SfcbItem;

        SfcbItem* sfcbs;

        void find_leaf(FlowNode* node) {
            NodeIterator* it = node->iterator();
            FlowNodePtr* cur;
            while ((cur = it->next()) != 0) {
                if (cur->is_leaf()) {
                    SfcbItem* item = new SfcbItem();
                    item->next = sfcbs;
                    item->sfcb = cur->leaf;
                    sfcbs = item;
                } else {
                    find_leaf(cur->node);
                }
            }
            if (node->default_ptr()->ptr != 0) {
                cur = node->default_ptr();
                if (cur->is_leaf()) {
                    SfcbItem* item = new SfcbItem();
                    item->next = sfcbs;
                    item->sfcb = cur->leaf;
                    sfcbs = item;
                } else {
                    find_leaf(cur->node);
                }
            }
        }

    public:

        LeafIterator(FlowNode* node) {
            sfcbs = 0;
            find_leaf(node);
        }

        FlowControlBlock* next() {
            if (!sfcbs)
                return 0;
            FlowControlBlock* leaf = sfcbs->sfcb;
            SfcbItem* next = sfcbs->next;
            delete sfcbs;
            sfcbs = next;
            return leaf;
        }
    };


    LeafIterator* leaf_iterator() {
        return new LeafIterator(this);
    }

    template<typename F>
    void traverse(F fnt) {
        NodeIterator* it = this->iterator();
        FlowNodePtr* cur;
        while ((cur = it->next()) != 0) {
            if (cur->is_leaf()) {
                fnt(cur->leaf);
            } else {
                cur->node->traverse(fnt);
            }
        }
        if (this->default_ptr()->ptr != 0) {
            cur = this->default_ptr();
            if (cur->is_leaf()) {
                fnt(cur->leaf);
            } else {
                cur->node->traverse(fnt);
            }
        }
    }

    void print() {
        click_chatter("---");
        print(this,"");
        click_chatter("---");
    }

    friend class FlowClassificationTable;
};

/**
 * Dummy FlowLevel used for the default path before merging a table
 */
class FlowLevelDummy  : public FlowLevel {
public:

    FlowLevelDummy() {

    }

    inline long unsigned get_max_value() {
        return 0;
    }

    inline FlowNodeData get_data(Packet* packet) {
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
    }

    FlowLevelAggregate() : FlowLevelAggregate(0,-1) {

    }


    int offset;
    uint32_t mask;

    inline long unsigned get_max_value() {
        return mask;
    }

    inline FlowNodeData get_data(Packet* packet) {
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

    }

    inline long unsigned get_max_value() {
        return _numthreads;
    }

    inline FlowNodeData get_data(Packet*) {
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

    bool equals(FlowLevel* level) {
        click_chatter("Offset equals");
        return ((FlowLevel::equals(level))&& (_offset == dynamic_cast<FlowLevelOffset*>(level)->_offset));
    }

};
/**
 * Flow level for any offset/mask of 8 bits
 */
class FlowLevelGeneric8 : public FlowLevelOffset {
private:


    uint8_t _mask;
public:

    FlowLevelGeneric8(uint8_t mask, int offset) : _mask(mask), FlowLevelOffset(offset) {

    }
    FlowLevelGeneric8() : FlowLevelGeneric8(0,0) {

    }
    void set_match(int offset, uint8_t mask) {
        _mask = mask;
        _offset = offset;
    }

    uint8_t mask() const {
        return _mask;
    }

    inline long unsigned get_max_value() {
        return _mask;
    }

    inline FlowNodeData get_data(Packet* packet) {
        return (FlowNodeData){.data_8 = (uint8_t)(*(packet->data() + _offset) & _mask)};
    }

    String print() {
        return String(_offset) + "/" + String((uint32_t)_mask) ;
    }


    FlowLevel* duplicate() override {
        return (new FlowLevelGeneric8(_mask,_offset))->assign(this);
    }
};

/**
 * Flow level for any offset/mask of 16 bits
 */
class FlowLevelGeneric16 : public FlowLevelOffset {
private:
    uint16_t _mask;
public:
    FlowLevelGeneric16(uint16_t mask, int offset) : _mask(mask), FlowLevelOffset(offset) {

    }

    FlowLevelGeneric16() : FlowLevelGeneric16(0,0) {

    }

    void set_match(int offset, uint16_t mask) {
        _mask = htons(mask);
        _offset = offset;
    }

    uint16_t mask() const {
        return _mask;
    }

    inline long unsigned get_max_value() {
        return _mask;
    }

    inline FlowNodeData get_data(Packet* packet) {
        FlowNodeData data;
        data.data_16 = (uint16_t)(*((uint16_t*)(packet->data() + _offset)) & _mask);
        return data;
    }

    String print() {
        return String(_offset) + "/" + String(_mask) ;
    }

    FlowLevel* duplicate() override {
        return (new FlowLevelGeneric16(_mask,_offset))->assign(this);
    }
};

/**
 * Flow level for any offset/mask of 32 bits
 */
class FlowLevelGeneric32 : public FlowLevelOffset {
private:
    uint32_t _mask;
public:


    FlowLevelGeneric32(uint32_t mask, int offset) : _mask(mask), FlowLevelOffset(offset) {

    }

    FlowLevelGeneric32() : FlowLevelGeneric32(0,0) {

    }
    void set_match(int offset, uint32_t mask) {
        _mask = htonl(mask);
        _offset = offset;
    }

    uint32_t mask() const {
        return _mask;
    }

    inline long unsigned get_max_value() {
        return _mask;
    }

    inline FlowNodeData get_data(Packet* packet) {
        FlowNodeData data;
        data.data_32 = (uint32_t)(*((uint32_t*) (packet->data() + _offset)) & _mask);
        return data;
    }

    String print() {
        return String(_offset) + "/" + String(_mask) ;
    }

    FlowLevel* duplicate() override {
        return (new FlowLevelGeneric32(_mask,_offset))->assign(this);
    }
};

/**
 * FlowLevel for any offset/mask of 64bits
 */
class FlowLevelGeneric64 : public FlowLevelOffset {
    uint64_t _mask;
public:

    FlowLevelGeneric64(uint64_t mask, int offset) : _mask(mask), FlowLevelOffset(offset) {

    }

    FlowLevelGeneric64() : _mask(0) {
        _islong = true;
    }

    void set_match(int offset, uint64_t mask) {
        _mask = __bswap_64(mask);
        _offset = offset;
    }

    uint64_t mask() const {
        return _mask;
    }

    inline long unsigned get_max_value() {
        return _mask;
    }

    inline FlowNodeData get_data(Packet* packet) {
        FlowNodeData data;
        data.data_64 = (uint64_t)(*((uint64_t*) (packet->data() + _offset)) & _mask);
        return data;
    }

    String print() {
        return String(_offset) + "/" + String(_mask) ;
    }

    FlowLevel* duplicate() override {
        return (new FlowLevelGeneric64(_mask,_offset))->assign(this);
    }
};

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

	virtual void renew() {
		_released = false;
	}

	virtual void release_child(FlowNode* child) {
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
    FlowNodeArray(unsigned int max_size) {
        childs.resize(max_size);
    }

    FlowNode* duplicate(bool recursive,int use_count) override {
        FlowNodeArray* fa = new FlowNodeArray(childs.size());
        fa->duplicate_internal(this,recursive,use_count);
        return fa;
    }

    FlowNodePtr* find(FlowNodeData data) {
        return &childs[data.data_32];
    }

    void inc_num() {
        num++;
    }

    String name() {
        return "ARRAY";
    }
    /*
	void _remove(FlowNode* child) {
		delete childs[child->data];
		childs[child->data] = NULL;
		num--;
	}*/

    virtual void renew() {
        _released = false;
        for (int i = 0; i < childs.size(); i++) {
            if (childs[i].ptr) {
                if (childs[i].is_leaf())
                    childs[i].leaf = 0;
                else
                    childs[i].node->release();
            }
        }
        num = 0;

    }

    void release_child(FlowNodePtr child) {
        if (_child_deletable) {
            if (child.is_leaf()) {
                childs[child.data().data_32].ptr = 0;
            } else {
                child.node->release();
            }
            num--;
        }
    }

    ~FlowNodeArray() {
        for (int i = 0; i < childs.size(); i++) {
            if (childs[i].ptr != NULL) {
                //if (!leaf)
                delete childs[i].node;
            }
        }
    }

    class ArrayNodeIterator : public NodeIterator{
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

    NodeIterator* iterator() {
        return new ArrayNodeIterator(this);
    }
};


/**
 * Node implemented using a hash table
 */
class FlowNodeHash : public FlowNode  {
    Vector<FlowNodePtr> childs;

    const static int HASH_SIZES_NR;
    const static int hash_sizes[];
    int size_n;
    int hash_size;
    uint32_t mask;
    int highwater;
    int max_highwater;
    unsigned int hash32(uint32_t d) {
        return (d + (d >> 8) + (d >> 16) + (d >> 24)) & mask;
        //return d % hash_size;
    }

    unsigned int hash64(uint64_t ld) {
        uint32_t d = (uint32_t)ld ^ (ld >> 32);
        return (d + (d >> 8) + (d >> 16) + (d >> 24)) & mask;
        //return d % hash_size;
    }


    inline int next_idx(int idx) {
        return (idx + 1) % hash_size;

    }

    String name() {
        return "HASH-" + String(hash_size);
    }

    inline unsigned get_idx(FlowNodeData data) {
        int i = 0;

        unsigned int idx;

        if (level()->is_long())
            idx = hash64(data.data_32);
        else
            idx = hash32(data.data_32);

#if DEBUG_CLASSIFIER
        click_chatter("Idx is %d, table v = %p",idx,childs[idx]);
#endif
        while (childs[idx].ptr && childs[idx].data().data_64 != data.data_64) {
            //click_chatter("Collision hash[%d] is taken by %x while searching space for %x !",idx,childs[idx].data().data_64, data.data_64);
            idx = next_idx(idx);
            i++;
        }


        if (i > 50) {
            click_chatter("%d collisions !",i);
            //resize();
        }

        return idx;
    }

    class HashNodeIterator : public NodeIterator{
        FlowNodeHash* _node;
        int cur;
    public:
        HashNodeIterator(FlowNodeHash* n) : cur(0) {
            _node = n;
        }

        FlowNodePtr* next() {
            while ( cur < _node->childs.size() && !_node->childs[cur].ptr) cur++;
            if (cur >= _node->childs.size())
                return 0;
            return &_node->childs[cur++];
        }
    };

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

    FlowNode* duplicate(bool recursive,int use_count) override {
        FlowNodeHash* fh = new FlowNodeHash();
        fh->duplicate_internal(this,recursive,use_count);
        return fh;
    }

    FlowNodeHash() :size_n(FlowNodeHash::HASH_SIZES_NR){
        hash_size = hash_sizes[size_n -1];
        mask = hash_size - 1;
        highwater = hash_size / 3;
        max_highwater = hash_size / 2;
        childs.resize(hash_size);
        for (int i = 0; i < hash_size; i++) {
            childs[i].ptr= NULL;
        }
    }




    FlowNodePtr* find(FlowNodeData data) {
        //click_chatter("Searching for %d in hash table leaf %d",data,leaf);

        unsigned idx = get_idx(data);

        //If it's a released node
        if (childs[idx].ptr && childs[idx].is_node() && _child_deletable && childs[idx].node->released()) {
            num++;
            /*if (num > max_highwater)
				resize();*/
        }
        //TODO : should not it be the contrary?

        return &childs[idx];
    }

    void inc_num() {
        num++;
        if (num > max_highwater)
            resize();

    }

    virtual void renew() {
        _released = false;
        for (int i = 0; i < hash_size; i++) {
            if (childs[i].ptr) {
                if (childs[i].is_leaf()) //TODO : should we release here?
                    childs[i].leaf = NULL;
                else
                    childs[i].node->release();;
            }
        }
        num = 0;
    }

    void release_child(FlowNodePtr child) {
        if (_child_deletable) {
            if (child.is_leaf()) {
                childs[get_idx(child.data())].ptr = NULL;
                num--;
            } else {
                child.node->release();;
                num--;
            }
        }
    }

    virtual ~FlowNodeHash() {
        for (int i = 0; i < hash_size; i++) {
            if (childs[i].ptr && !childs[i].is_leaf())
                delete childs[i].node;
        }
    }

    NodeIterator* iterator() {
        return new HashNodeIterator(this);
    }
};


/**
 * Dummy node for root with one child
 */
class FlowNodeDummy : public FlowNode {

    class DummyIterator : public NodeIterator{
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

    String name() {
        return "DUMMY";
    }

    FlowNode* duplicate(bool recursive,int use_count) override {
        FlowNodeDummy* fh = new FlowNodeDummy();
        fh->duplicate_internal(this,recursive,use_count);
        return fh;
    }

    FlowNodeDummy() {
    }

    void release_child(FlowNodePtr child) {
        click_chatter("TODO");
    }

    FlowNodePtr* find(FlowNodeData data) {
        return &_default;
    }

    virtual ~FlowNodeDummy() {
    }

    NodeIterator* iterator() {
        return new DummyIterator(this);
    }
};

/**
 * Node supporting only one value and a default value
 */
class FlowNodeTwoCase : public FlowNode  {
    FlowNodePtr child;

    class TwoCaseIterator : public NodeIterator{
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
    String name() {
        return "TWOCASE";
    }

    FlowNode* duplicate(bool recursive,int use_count) override {
        FlowNodeTwoCase* fh = new FlowNodeTwoCase(child);
        fh->duplicate_internal(this,recursive,use_count);
        return fh;
    }

    FlowNodeTwoCase(FlowNodePtr c) : child(c) {
        c.set_parent(this);
    }

    FlowNodePtr* find(FlowNodeData data) {
        if (data.data_64 == child.data().data_64)
            return &child;
        else
            return &_default;
    }

    virtual void renew() {
        click_chatter("TODO");
        /*
		_released = false;
		if (child.ptr) {
			if (is_leaf())
				child.leaf = NULL;
			else
				child.node->release();;
		}
		num = 0;*/
    }

    void release_child(FlowNodePtr child) {
        click_chatter("TODO");
        /*if (_child_deletable) {
			if (is_leaf()) {
				childs[get_idx(child.data())].ptr = NULL;
				num--;
			} else {
				child.node->release();;
				num--;
			}
		}*/
    }

    virtual ~FlowNodeTwoCase() {
        if (child.ptr) //&& !leaf)
            delete child.node;
    }

    NodeIterator* iterator() {
        return new TwoCaseIterator(this);
    }
};

/**
 * Node supporting only one value and a default value
 */
class FlowNodeThreeCase : public FlowNode  {
    FlowNodePtr childA;
    FlowNodePtr childB;

    class ThreeCaseIterator : public NodeIterator{
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
    String name() {
        return "THREECASE";
    }

    FlowNode* duplicate(bool recursive,int use_count) override {
        FlowNodeThreeCase* fh = new FlowNodeThreeCase(childA,childB);
        fh->duplicate_internal(this,recursive,use_count);
        return fh;
    }

    FlowNodeThreeCase(FlowNodePtr a,FlowNodePtr b) : childA(a),childB(b) {

    }

    FlowNodePtr* find(FlowNodeData data) {
        if (data.data_64 == childA.data().data_64)
            return &childA;
        else if (data.data_64 == childB.data().data_64)
            return &childB;
        else
            return &_default;
    }

    virtual void renew() {
        click_chatter("TODO");
        /*
		_released = false;
		if (child.ptr) {
			if (is_leaf())
				child.leaf = NULL;
			else
				child.node->release();;
		}
		num = 0;*/
    }

    void release_child(FlowNodePtr child) {
        click_chatter("TODO");
        /*if (_child_deletable) {
			if (is_leaf()) {
				childs[get_idx(child.data())].ptr = NULL;
				num--;
			} else {
				child.node->release();;
				num--;
			}
		}*/
    }

    virtual ~FlowNodeThreeCase() {

    }

    NodeIterator* iterator() {
        return new ThreeCaseIterator(this);
    }
};

class FlowClassificationTable {
public:
    FlowClassificationTable();

    void set_release_fnt(SubFlowRealeaseFnt pool_release_fnt);
    FCBPool& get_pool();
    void set_root(FlowNode* node);
    FlowNode* get_root();

    inline FlowControlBlock* match(Packet* p,bool always_dup);
    inline bool reverse_match(FlowControlBlock* sfcb, Packet* p);

private:
    FlowNode* _root;
    FCBPool _pool;
    SubFlowRealeaseFnt _pool_release_fnt;
};


/*****************************************************
 * Inline implementations for performances
 *****************************************************/

/**
 * Check that a SFCB match correspond to a packet
 * @assert the FCB does not follow any default rule
 */
bool FlowClassificationTable::reverse_match(FlowControlBlock* sfcb, Packet* p) {
    FlowNode* parent = (FlowNode*)sfcb->parent;
    //assert(parent->default_ptr()->ptr != sfcb);
    if (unlikely(!parent->_leaf_reverse_match(sfcb,p))) {
#if DEBUG_CLASSIFIER_MATCH > 2
        click_chatter("DIF is_default %d Leaf %x %x level %s",parent->default_ptr()->ptr == sfcb,parent->level()->get_data(p).data_64, sfcb->node_data[0].data_64,parent->level()->print().c_str());
#endif
        return false;
    } else {
#if DEBUG_CLASSIFIER_MATCH > 2
        click_chatter("MAT is_default %d Leaf %x %x level %s",parent->default_ptr()->ptr == sfcb,parent->level()->get_data(p).data_64, sfcb->node_data[0].data_64,parent->level()->print().c_str());
#endif
    }

    do {
        FlowNode* child = parent;
        parent = parent->parent();

        if (likely(!parent->_node_reverse_match(child,p))) {
#if DEBUG_CLASSIFIER_MATCH > 2
            click_chatter("DIF is_default %d Child %x %x level %s",parent->default_ptr()->ptr == child ,parent->level()->get_data(p).data_64, child->node_data.data_64,parent->level()->print().c_str());
#endif
            return false;
        } else {
#if DEBUG_CLASSIFIER_MATCH > 2
            click_chatter("MAT is_default %d Child %x %x level %s",parent->default_ptr()->ptr == child,parent->level()->get_data(p).data_64, child->node_data.data_64,parent->level()->print().c_str());
#endif
        }
    } while (parent != _root);
    return true;
}


FlowControlBlock* FlowClassificationTable::match(Packet* p,bool always_dup) {
    always_dup = false;
    FlowNode* parent = _root;
    FlowNodePtr* child_ptr = 0;
#if DEBUG_CLASSIFIER
    int level_nr = 0;
#endif
    do {
        FlowNodeData data = parent->level()->get_data(p);
#if DEBUG_CLASSIFIER_MATCH > 1
        click_chatter("[%d] Data level %d is %016llX",click_current_cpu_id(), level_nr++,data.data_64);
#endif
        child_ptr = parent->find(data);
#if DEBUG_CLASSIFIER_MATCH > 1
        click_chatter("->Ptr is %p, is_leaf : %d",child_ptr->ptr, child_ptr->is_leaf());
#endif
        if (child_ptr->ptr == NULL) {
            if (parent->get_default().ptr) {
                if (parent->level()->is_dynamic() || always_dup) {
                    parent->inc_num();
                    if (parent->get_default().is_leaf()) { //Leaf are not duplicated, we need to do it ourself
#if DEBUG_CLASSIFIER_MATCH > 1
                        click_chatter("DUPLICATE leaf");
#endif
                        //click_chatter("New leaf with data '%x'",data.data_64);
                        //click_chatter("Data %x %x",parent->default_ptr()->leaf->data_32[2],parent->default_ptr()->leaf->data_32[3]);
                        child_ptr->set_leaf(_pool.allocate());
                        child_ptr->leaf->initialize();
                        child_ptr->leaf->parent = parent;
                        child_ptr->leaf->release_fnt = _pool_release_fnt;
                        child_ptr->set_data(data);
                        memcpy(&child_ptr->leaf->node_data[1], &parent->default_ptr()->leaf->node_data[1] ,_pool.data_size() - sizeof(FlowNodeData));
#if DEBUG_CLASSIFIER_MATCH > 3
                        _root->print();
#endif
                        return child_ptr->leaf;
                    } else {
                        click_chatter("DUPLICATE node");
                        child_ptr->set_node(parent->default_ptr()->node->duplicate(false, 0));
                        child_ptr->set_data(data);
                        child_ptr->set_parent(parent);
                    }
                } else {
                    child_ptr = parent->default_ptr();
                    if (child_ptr->is_leaf())
                        return child_ptr->leaf;
                }
            } else {
                click_chatter("ERROR : no classification node and no default path !");
                return 0;
            }
        } else if (child_ptr->is_leaf()) {
            return child_ptr->leaf;
        } else { // is an existing node
            if (child_ptr->node->released()) {
                child_ptr->node->renew();
                child_ptr->set_data(data);
            }
        }
        parent = child_ptr->node;

        assert(parent);
    } while(1);



    /*int action_id = 0;
	  //=OXM_OF_METADATA_W
	struct ofp_action_header* action = action_table[action_id];
	switch (action->type) {
		case OFPAT_SET_FIELD:
			struct ofp_action_set_field* action_set_field = (struct ofp_action_set_field*)action;
			struct ofp_match* match = (struct ofp_match*)action_set_field->field;
			switch(match->type) {
				case OFPMT_OXM:
					switch (OXM_HEADER(match->oxm_fields)) {
						case OFPXMC_PACKET_REGS:

					}
					break;
				default:
					click_chatter("Error : action field type %d unhandled !",match->type);
			}
			break;
		default:
			click_chatter("Error : action type %d unhandled !",action->type);
	}*/
}

inline FlowNodeData FlowNodePtr::data() {
    if (is_leaf())
        return leaf->node_data[0];
    else
        return node->node_data;
}

inline FlowNode* FlowNodePtr::parent() {
    if (is_leaf())
        return (FlowNode*)leaf->parent;
    else
        return node->parent();
}

inline void FlowNodePtr::set_data(FlowNodeData data) {
    if (is_leaf())
        leaf->node_data[0] = data;
    else
        node->node_data = data;
}

inline void FlowNodePtr::set_parent(FlowNode* parent) {
    if (is_leaf())
        leaf->parent = parent;
    else
        node->set_parent(parent);
}

inline FlowNode* FlowNode::create(FlowNode* parent, FlowLevel* level) {
    FlowNode * fl;
    //click_chatter("Level max is %u, deletable = %d",level->get_max_value(),level->deletable);
    if (level->get_max_value() == 0)
        fl = new FlowNodeDummy();
    else if (level->get_max_value() > 256)
        fl = new FlowNodeHash();
    else
        fl = new FlowNodeArray(level->get_max_value());
    fl->_level = level;
    fl->_child_deletable = level->is_deletable();
    fl->_parent = parent;
    return fl;
}


#endif

CLICK_ENDDECLS
#endif
