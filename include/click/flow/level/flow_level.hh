#ifndef CLICK_FLOWLEVEL_HH
#define CLICK_FLOWLEVEL_HH 1
#include <click/packet.hh>
#if HAVE_DPDK
#include <rte_flow.h>
#endif
#include "../common.hh"

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
    void print(int data_offset = -1) const;

    //---
    //Compile time functions
    //---

    FlowNodePtr optimize(Bitvector threads);

    bool else_drop();

    inline void traverse_all_leaves(std::function<void(FlowNodePtr*)>);

    inline void check();

    bool replace_leaf_with_node(FlowNode*, bool discard, Element* origin);

    void node_combine_ptr(FlowNode* parent, FlowNodePtr, bool as_child, bool priority, bool no_dynamic, Element* origin);
    void default_combine(FlowNode* parent, FlowNodePtr*, bool as_child, bool priority, Element* origin);
};



class FlowLevel {
private:
    //bool deletable;
protected:
    bool _dynamic;
#if HAVE_LONG_CLASSIFICATION
    bool _islong = false;
#endif
public:
    FlowLevel() :
        //deletable(true),
        _dynamic(false) {

    };

    typedef FlowNodeData (*GetDataFn)(void*, Packet* packet);
    GetDataFn _get_data;

    virtual ~FlowLevel() {};

    /**
     * Maximum value this level can return, included.
     * Eg 255 for an array of 256 values. In most cases this is actually
     *  equal to the mask.
     */
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
     *  @return true if something changed
     */
    virtual bool prune(FlowLevel*) {return false;};

    virtual FlowNodePtr prune(FlowLevel* other, FlowNodeData data, FlowNode* node, bool &changed);

    /**
     * Tell if two node are of the same type, and on the same field/value/mask if applicable
     *
     * However this is not checking if runtime data and dynamic are equals
     */
    virtual bool equals(FlowLevel* level) {
        return typeid(*this) == typeid(*level) &&
#if HAVE_LONG_CLASSIFICATION
                _islong == level->_islong &&
#endif
                _dynamic == level->_dynamic;
    }

    inline FlowNodeData get_data(Packet* p) {
        flow_assert(_get_data);
        flow_assert(this);
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
#if HAVE_LONG_CLASSIFICATION
    inline bool is_long() const {
        return _islong;
    }
#else
    inline bool is_long() const {
        return false;
    }
#endif
    FlowLevel* assign(FlowLevel* l) {
        _dynamic = l->_dynamic;
        //deletable = l->deletable;
#if HAVE_LONG_CLASSIFICATION
        _islong = l->_islong;
#endif
        return this;
    }

    virtual FlowLevel *duplicate() = 0;

    virtual FlowLevel *optimize(FlowNode*) {
        return this;
    }

#if HAVE_DPDK
    virtual int to_dpdk_flow(FlowNodeData data, rte_flow_item_type last_layer, int offset, rte_flow_item_type &next_layer, int &next_layer_offset, Vector<rte_flow_item> &pattern, bool is_default) {
        return -1;
    }
#endif

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
/*        click_chatter("FlowLevelDummy should be stripped !");
        abort();*/
        return FlowNodeData();
    }

    String print() {
        return String("ANY");
    }

    FlowLevel* duplicate() override {
        return (new FlowLevelDummy())->assign(this);
    }

#if HAVE_DPDK
    virtual int to_dpdk_flow(FlowNodeData data, rte_flow_item_type last_layer, int offset, rte_flow_item_type &next_layer, int &next_layer_offset, Vector<rte_flow_item> &pattern, bool is_default) override {
        rte_flow_item pat;
        pat.type = RTE_FLOW_ITEM_TYPE_VOID;
        next_layer = last_layer;
        next_layer_offset = offset;
        pattern.push_back(pat);
        return 1;
    }
#endif

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
    }
    FLOW_LEVEL_DEFINE(FlowLevelThread,get_data_thread);

    virtual bool is_mt_safe() const override { return true;};
    inline long unsigned get_max_value() {
        return _numthreads - 1;
    }

    inline FlowNodeData get_data_thread(Packet*) {
        return FlowNodeData((uint32_t)click_current_cpu_id());
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

#if HAVE_DPDK
    virtual int to_dpdk_flow(FlowNodeData data, rte_flow_item_type last_layer, int offset, rte_flow_item_type &next_layer, int &next_layer_offset, Vector<rte_flow_item> &pattern, bool is_default) override;
#endif
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

    FlowLevelGeneric(T mask, int offset) : FlowLevelOffset(offset),  _mask(mask) {
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

    virtual uint8_t get_mask(int o) const override {
        if (o < _offset) //2 < 0
            return 0;
        if (o >= _offset + mask_size())
            return 0;
        return ((uint8_t*)&_mask)[o-_offset];
    }

    virtual bool is_usefull() override {
        return _mask != 0;
    }

    //Remove from the mask what is already set in other
    virtual bool prune(FlowLevel* other) override;

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

    String print();

    FlowLevel* duplicate() override {
        return (new FlowLevelGeneric<T>(_mask,_offset))->assign(this);
    }

    bool equals(FlowLevel* level) {
        return ((FlowLevelOffset::equals(level)) && (_mask == dynamic_cast<FlowLevelGeneric<T>*>(level)->_mask));
    }


    virtual FlowLevel *optimize(FlowNode* parent) override;
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
        if (o >= _offset + mask_size())
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
        //click_chatter("FlowLevelField %d %x, sz %d",_offset,*(T*)(packet->data() + _offset),sizeof(T));
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
#if HAVE_LONG_CLASSIFICATION
using FlowLevelGeneric64 = FlowLevelGeneric<uint64_t>;
#endif
using FlowLevelField8 = FlowLevelField<uint8_t>;
using FlowLevelField16 = FlowLevelField<uint16_t>;
using FlowLevelField32 = FlowLevelField<uint32_t>;
#if HAVE_LONG_CLASSIFICATION
using FlowLevelField64 = FlowLevelField<uint64_t>;
#endif

#endif
