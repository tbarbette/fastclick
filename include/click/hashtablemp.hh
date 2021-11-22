#ifndef CLICK_HASHTABLEMP_HH
#define CLICK_HASHTABLEMP_HH
#include <click/glue.hh>
#include <click/hashcode.hh>
#include <click/hashcontainer.hh>
#include <click/allocator.hh>
#include <functional>
#include <click/multithread.hh>
#if CLICK_DEBUG_HASHMAP
# define click_hashmp_assert(x) assert(x)
#else
# define click_hashmp_assert(x)
#endif

#define HASTABLEMP_POW2_SZ 1

CLICK_DECLS

//Strut serving as constant template parameters
// MT_T is used when multithreading could be enabled
// NOMT when MT is always disabled
struct MT_T {
    bool _mt;
    MT_T(bool mt = true) {
        _mt = mt;
    }

    inline bool mt() const {
        return _mt;
    }

    inline void disable_mt() {
        _mt = false;
    }
};

struct NOMT {
    NOMT(bool) {};
    inline bool mt() const { return false; };
    inline void disable_mt() { };
};

/** @class HashContainerMP
  @brief Intrusive hash table template, MT safe version.

  K is the type of the key
  V is the type of the value
  Item is the value type enclosed by a reference counting mechanism based
   on shared pointers like shared<V> or rw_shared<V>, depending on the
   level of protection you want.

  For convenience HashTableMP is an alias for HashContainerMP<K,V,shared<V>>
*/
template <typename K, typename V, typename Item, typename MT=MT_T>
class HashContainerMP { public:

    class ListItem { public:
        ListItem(const ListItem &li) : item(li.item), key(li.key), _hashnext(0) {};
        ListItem(const K &k,const V &v) : item(v), key(k), _hashnext(0) {};
        Item item;
        K key;
        ListItem* _hashnext;
    };

    pool_allocator_mt<ListItem, false, 2048> _allocator;

    class ListItemPtr { public:
        ListItemPtr() : head(0) {};
        ListItem* head;
    };

    class Bucket { public:
        Bucket() : list() {

        }
        __rwlock<ListItemPtr> list;
    };

    class Table { public:
        Table() :
#if HASTABLEMP_POW2_SZ
         _nbuckets_mask(0),
#else
         _nbuckets(0),
#endif
         buckets(0) {
            _size = 0;
        }
        uint32_t nbuckets() {
#if HASTABLEMP_POW2_SZ
            return _nbuckets_mask + 1;
#else
            return _nbuckets;
#endif
        }
        #if HASTABLEMP_POW2_SZ
        uint32_t _nbuckets_mask;
        #else
        uint32_t _nbuckets;
        #endif
        Bucket* buckets;
        atomic_uint32_t _size;
    private:
        Table(const Table &o) {};
        void operator=(const Table &o){};
    };

    typedef typename Item::ptr ptr;
    typedef typename Item::write_ptr write_ptr;

    class iterator {
        mutable HashContainerMP<K,V,Item,MT>* _h;
        int _b;
        ListItem* _prev;
        ListItem* _item;
    public:
        ~iterator() {
            if (_h) {
                if (_b < _h->_table->nbuckets()) {
                    if (likely(_h->mt()))
                        _h->_table->buckets[_b].list.read_end();
                }
                if (likely(_h->mt()))
                    _h->_table.read_end();
            }
        }

        /** @brief Advance this iterator to the next element. */
        void operator++() {
            if (_b == _h->_table->nbuckets()) return;
            if (_item) {
                _item = _item->_hashnext;
                _prev = _item;
            }
            while (!_item) {
                //click_chatter("Bucket %d : %p",_b,_h->_table->buckets[_b]);
                if (likely(_h->mt()))
                    _h->_table->buckets[_b].list.read_end();
                if (++_b == _h->_table->nbuckets()) return;
                if (likely(_h->mt()))
                    _h->_table->buckets[_b].list.read_begin();
                _item = _h->_table->buckets[_b].list->head;
                _prev = 0;
            }
        }

        void operator++(int) {
            operator++();
        }


        operator bool() {
            return _item;
        }

        const K& key() {
            return _item->key;
        }

        ptr operator *() {
            return _item->item.read();
        }

        ptr operator ->() {
            return _item->item.read();
        }


        iterator(const iterator &o) {
            _h = o._h;
            _prev = o._prev;
            _item = o._item;
            _b = o._b;
            o._h = 0;
        }

        protected:
        //global read lock must be held!
        iterator(HashContainerMP<K,V,Item,MT>* h) : _h(h), _prev(0) {
            _b = 0;
            if (likely(_h->mt()))
                _h->_table->buckets[_b].list.read_begin();
            _item =  _h->_table->buckets[_b].list->head;
            if (_item == 0) {
                (*this)++;
            }
        }
        friend class HashContainerMP<K,V,Item,MT>;
    };

    class write_iterator {
        mutable HashContainerMP<K,V,Item,MT>* _h;
        int _b;
        ListItem* _prev;
        ListItem* _item;
    public:
        ~write_iterator() {
            if (_h) {
                if (_b < _h->_table->nbuckets()) {
                    if (likely(_h->mt()))
                        _h->_table->buckets[_b].list.write_end();
                }
                if (likely(_h->mt()))
                    _h->_table.read_end();
            }
            _h = 0; //Prevent read destruction
        }

        /** @brief Advance this iterator to the next element. */
        void operator++() {
            if (_b == _h->_table->nbuckets()) return;
            if (_item) {
                _item = _item->_hashnext;
                _prev = _item;
            }
            while (!_item) {
                if (likely(_h->mt()))
                    _h->_table->buckets[_b].list.write_end();
                if (++_b == _h->_table->nbuckets()) return;
                if (likely(_h->mt()))
                    _h->_table->buckets[_b].list.write_begin();
                _item = _h->_table->buckets[_b].list->head;
                _prev = 0;
            }
        }

        void operator++(int) {
            operator++();
        }


        operator bool() {
            return _item;
        }

        const K& key() {
            return _item->key;
        }

        write_ptr operator *() {
            return _item->item.write();
        }

        write_ptr operator ->() {
            return _item->item.write();
        }

        void erase() {
            if (_prev) {
                _prev->_hashnext = _item->_hashnext;
            } else {
                _h->_table->buckets[_b].list->head = _item->_hashnext;
            }
            ListItem* next = _item->_hashnext;
            _h->erase_item(_item);
            _item = next;
            if (next == 0)
                ++(*this);
        }

    protected:
        //global read lock must be held!
        write_iterator(HashContainerMP<K,V,Item,MT>* h) : _h(h), _prev(0) {
            _b = 0;
            if (likely(h->mt()))
                _h->_table->buckets[_b].list.write_begin();
            _item =  _h->_table->buckets[_b].list->head;
            if (_item == 0) {
                (*this)++;
            }
        }
        friend class HashContainerMP<K,V,Item,MT>;
    };

    static ListItem *&hashnext(ListItem *e) {
    return e->_hashnext;
    }
    static const K& hashkey(const ListItem *e) {
    return e->key;
    }
    static bool hashkeyeq(const K &a, const K &b) {
    return a == b;
    }

    typedef uint32_t size_type;

    enum {
#if CLICK_LINUXMODULE
    max_bucket_count = 4194303,
#else
    max_bucket_count = (uint32_t) -1,
#endif
    initial_bucket_count = 63
    };

    /** @brief Construct an empty HashContainer. */
    HashContainerMP();

    /** @brief Construct an empty HashContainer with at least @a n buckets. */
    explicit HashContainerMP(size_type n);

    /** @brief Destroy the HashContainer. */
    ~HashContainerMP();


    /** @brief Return the number of elements stored. */
    inline size_type size() {
        size_type s;
        if (likely(mt()))
            _table.read_begin();
        s = _table->_size;
        if (likely(mt()))
            _table.read_end();
        return s;
    }

    /** @brief Return the number of buckets. */
    inline size_type buckets() {
        size_type s;
        if (likely(mt()))
            _table.read_begin();
        s = _table->nbuckets();
        if (likely(mt()))
            _table.read_end();
        return s;
    }

    /** @brief Return true iff size() == 0. */
    inline bool empty() {
        return size() == 0;
    }


    /** @brief Return true if this HashContainer should be rebalanced. */
    inline bool unbalanced() {
        bool r;
        if (likely(mt()))
            _table.read_begin();
        r = _table->_size > 2 * _table->nbuckets() && _table->nbuckets() < max_bucket_count;
        if (likely(mt()))
            _table.read_end();
        return r;
    }

    inline iterator begin();

    inline write_iterator write_begin();

    //Searchs
    /** @brief Test if an element with key @a key exists in the table. */
    inline bool contains(const K& key);

    /** @brief Return a read pointer for the element with @a key, if any. */
    inline ptr find(const K &key);

    /** @brief Return a write pointer for the element with @a key, if any. */
    inline write_ptr find_write(const K &key);

    /** @brief Find a key K and call store if found. Calls clean on colliding entries upon the way */
    inline bool find_clean(const K &key, std::function<void(V &storage)> store, std::function<bool(V &storage)> shouldremove);

    /** @brief Copy the value of V for K in storage if K is found, delete it from the table and return true. If nonfound, nothing is deleted and returns false. */
    inline bool find_erase(const K &key, V &storage);

    /** @brief Find a key, and call store if found before removing it. Calls clean on colliding entries upon the way */
    inline bool find_erase_clean(const K &key, std::function<void(V &storage)> store, std::function<bool(V &storage)> shouldremove);

    //Insertions that do not replace
    /* @brief find a key, and if it does not exists, inserts the given value. Return a read pointer to the entry. */
    inline ptr find_insert(const K &key, const V &value);

    /* @brief find a key, and if it does not exists, inserts the given value. Return a write pointer to the entry. */
    inline write_ptr find_insert_write(const K &key, const V &value);

    /* Looks for a key, if it does not exists on_create is called to "create" the value to be inserted.
     * This is very similar to find_insert, but is useful for cases where the object should only be constructed if it does not already exists,
     * this is more performant that using find/contains, then insert a newly created value, rehashing again etc. Also it is "atomic". */
    inline ptr find_create(const K &key,std::function<V(void)> on_create);

    //Replacing insertions
    /** @brief Insert a key. Replace an item if it already exists, insert it if not. Gives the ability to do something (eg free resources) with the previous element. Does not look at use count for you and done under read lock !
     * The difference with find_insert, is that find_insert will not change the value if the value already existsn while here on_replace() will be called, and then the value will be replaced.
     * on_replace() is NOT called if the value does not exists.
     * This function was called set() before*/
    inline void insert(const K &key, const V &value, std::function<void(V&value)> on_replace = [](V&){});

    /* @brie Insert a key in the table, calling clean on collisions. If a key exists, it is replaced. */
    inline void insert_clean(const K &key, const V &value, std::function<bool(V &storage)> shouldremove);

    //Simple removal
    inline void erase(const K &key);

    void clear();

    void rehash(size_type n);

    inline bool alone(bool atomic = false);


    uint32_t refcnt() {
        return _table.refcnt();
    }

    inline void disable_mt() {
        _mt.disable_mt();
    }

    inline bool mt() {
        return _mt.mt();
    }

    inline void resize_clear(uint32_t n) {
        deinitialize();
        initialize(n);
    }
  protected:
    MT _mt;

    __rwlock<Table> _table;
    per_thread<ListItem*> _pending_release;

    void initialize(size_type n);
    void deinitialize();

    HashContainerMP(const HashContainerMP<K,V,Item,MT> &);
    HashContainerMP<K,V,Item,MT> &operator=(const HashContainerMP<K,V,Item,MT> &);

    //_table read lock must be held
    size_type bucket(const K &key);

    //Write lock of the bucket must be held, and the item be out of the structure
    void erase_item(ListItem* item) {
        if (item->item.unshared()) { //Safe because we have the write lock, so nobody can take this element now
            _allocator.release(item);
        } else {
            item->_hashnext = _pending_release.get();
            _pending_release.set(item);
        }
    }

    ListItem* allocate(const ListItem &li) {
        ListItem* it;
        if ((it = _pending_release.get()) && it->item.unshared()) {
            _pending_release.set(it->_hashnext);
            return it;
        }
        return _allocator.allocate(li);
    }

    void release_pending(bool force=false) {
        ListItem* it;
        ListItem* next;
        for (unsigned i = 0; i < _pending_release.weight() ; i++) {
            it = _pending_release.get_value(i);
            while (it) {
                next = it->_hashnext;
                _allocator.release(it);
                it = next;
            }
        }
    }

    template <bool do_remove>
    inline bool _find_clean(const K &key, std::function<void(V &storage)> store, std::function<bool(V &storage)> shouldremove, std::function<void(ListItem* &head)> on_missing = [](ListItem* &head){});


    friend class iterator;

};

template <typename K, typename V, typename Item, typename MT>
void HashContainerMP<K,V,Item,MT>::initialize(size_type n)
{
    _table->_size = 0;
#if HASTABLEMP_POW2_SZ
    _table->_nbuckets_mask = next_pow2(n) - 1;
#else
    _table->_nbuckets = n;
#endif
    _table->buckets = (Bucket *) CLICK_LALLOC(sizeof(Bucket) * _table->nbuckets());
    for (size_type b = 0; b < _table->nbuckets(); ++b)
            new(&_table->buckets[b]) Bucket();
}

template <typename K, typename V, typename Item, typename MT>
void HashContainerMP<K,V,Item,MT>::deinitialize()
{
    for (size_type b = 0; b < _table->nbuckets(); ++b)
        _table->buckets[b].~Bucket();
    CLICK_LFREE(_table->buckets, sizeof(Bucket) * _table->nbuckets());
}


template <typename K, typename V, typename Item, typename MT>
HashContainerMP<K,V,Item,MT>::HashContainerMP() :
  _mt(true), _table(), _pending_release(0)
{
    initialize(initial_bucket_count);
}

template <typename K, typename V, typename Item, typename MT>
HashContainerMP<K,V,Item,MT>::HashContainerMP(size_type nb) :
     _mt(true), _table(), _pending_release(0)
{
    size_type b = 1;
    while (b < nb && b < max_bucket_count)
    b = ((b + 1) << 1) - 1;

    initialize(b);
}

template <typename K, typename V, typename Item, typename MT>
HashContainerMP<K,V,Item,MT>::~HashContainerMP()
{
    clear();
    release_pending(true);
    deinitialize();
}
#define MAKE_FIND(ptr_type) \
        if (likely(mt()))\
            _table.read_begin();\
        size_type b = bucket(key);\
        Bucket& bucket = _table->buckets[b];\
        if (likely(mt()))\
            bucket.list.read_begin();\
        if (likely(mt()))\
            _table.read_end();\
        ListItem *pprev;\
        ptr_type p;\
        for (pprev = bucket.list->head; pprev; pprev = hashnext(pprev))\
        if (hashkeyeq(hashkey(pprev), key)) {\
            p.assign(&pprev->item);\
            break;\
        }\
        if (likely(mt()))\
            bucket.list.read_end();\

template <typename K, typename V, typename Item, typename MT>
inline typename HashContainerMP<K,V,Item,MT>::ptr
HashContainerMP<K,V,Item,MT>::find(const K &key)
{
    MAKE_FIND(ptr);
    return p;
}

template <typename K, typename V, typename Item, typename MT>
inline typename HashContainerMP<K,V,Item,MT>::write_ptr
HashContainerMP<K,V,Item,MT>::find_write(const K &key)
{
    MAKE_FIND(write_ptr);
    return p;
}

/**
 * Find a key and then removes it from the table if do_remove is true.
 * While going through collisions, entries are removed if shoulddelete returns true.
 * Calls store to save the found entry to eg a temporary variables. Return true if found, false if not.
 *
 */
template <typename K, typename V, typename Item, typename MT> template <bool do_remove>
inline bool
HashContainerMP<K,V,Item,MT>::_find_clean(const K &key, std::function<void(V &c)> store, std::function<bool(V &c)> shoulddelete, std::function<void(ListItem* &head)> on_missing)
{
    if (likely(mt()))
        _table.read_begin();
    size_type b = bucket(key);
    Bucket& bucket = _table->buckets[b];
    if (likely(mt()))
        bucket.list.write_begin();
    if (likely(mt()))
        _table.read_end();
    ListItem *pprev = bucket.list->head;
    ListItem* *pprev_ptr = &bucket.list->head;
    while (pprev) {
        //We have the write lock of the bucket, so cleaning along the way is fine
        if (shoulddelete(*pprev->item.unprotected_ptr())) {
            *pprev_ptr = pprev->_hashnext;
            ListItem* tmp = pprev;
            pprev = *pprev_ptr;
            erase_item(tmp);
            _table->_size--;
        } else {
            if (hashkeyeq(hashkey(pprev), key)) {
                store(*pprev->item.unprotected_ptr());
                if (do_remove) {
				*pprev_ptr = pprev->_hashnext;
					erase_item(pprev);
					_table->_size--;
                }
                goto found;
            }
            pprev_ptr = &pprev->_hashnext;
            pprev = pprev->_hashnext;
        }
    }
    on_missing(bucket.list->head);
    if (likely(mt()))
        bucket.list.write_end();
    return false;
    found:
    if (likely(mt()))
        bucket.list.write_end();
    return true;
}

/**
 *
 * XXX Store is not respectful of the item lock
 * Cleaning and removal is done respectfully of the item lock. It is remove right away but not freed until references expire.
 */
template <typename K, typename V, typename Item, typename MT>
inline bool
HashContainerMP<K,V,Item,MT>::find_erase_clean(const K &key, std::function<void(V &c)> store, std::function<bool(V &c)> shoulddelete)
{
	return _find_clean<true>(key, store, shoulddelete);
}

/**
 * XXX Store is not respectful of the item lock
 * Cleaning and removal is done respectfully of the item lock. It is remove right away but not freed until references expire.
 */
template <typename K, typename V, typename Item, typename MT>
inline bool
HashContainerMP<K,V,Item,MT>::find_clean(const K &key, std::function<void(V &c)> store, std::function<bool(V &c)> shoulddelete)
{
	return _find_clean<false>(key, store, shoulddelete);
}

/**
 * XXX Replacement if the item already exists is not respectful of the item lock
 * Cleaning is done respectfully of the item lock. It is removed right away but not freed until references expire.
 */
template <typename K, typename V, typename Item, typename MT>
inline void
HashContainerMP<K,V,Item,MT>::insert_clean(const K &key, const V& value, std::function<bool(V &c)> shoulddelete)
{
    auto inserter = [key,value,this](ListItem* &head){
		ListItem* e = allocate(ListItem(key,value));
		*e->item.unprotected_ptr() = value;
		click_hashmp_assert(e->item.unshared());
		e->_hashnext = head;
		head = e;
		_table->_size++;
	};
	_find_clean<false>(key, [&value](V&c){c = value;}, shoulddelete, inserter);
}


/**
 * Find and return an entry, before removing it from the table
 *
 * Returns true if the entry was found (and therefore storage has been writen)
 */
template <typename K, typename V, typename Item, typename MT>
inline bool
HashContainerMP<K,V,Item,MT>::find_erase(const K &key, V &storage) {
    return find_erase_clean(key, [&storage](V&c){storage = c;}, [](V&){return false;});
}

/**
 * Erase an entry from the table
 *
 * Do nothing if the entry is not found.
 */
template <typename K, typename V, typename Item, typename MT>
inline void
HashContainerMP<K,V,Item,MT>::erase(const K &key) {
    find_erase_clean(key, [](V&){}, [](V&){return false;});
}

#define MAKE_FIND_INSERT(ptr_type,on_exists,value) \
    if (likely(mt()))\
        _table.read_begin();\
    size_type b = bucket(key);\
    Bucket& bucket = _table->buckets[b];\
\
retry:\
    if (likely(mt()))\
        bucket.list.read_begin();\
    \
    ListItem *pprev;\
    ptr_type p;\
    for (pprev = bucket.list->head; pprev; pprev = hashnext(pprev)) {\
        if (hashkeyeq(hashkey(pprev), key)) {\
            p.assign(&pprev->item);\
            break;\
        }\
    }\
    if (p) {\
        on_exists(pprev->item);\
        if (likely(mt()))\
            bucket.list.read_end();\
    } else {\
        if (likely(mt()) && !bucket.list.read_to_write()) {\
            goto retry;\
        }\
        ListItem* e = allocate(ListItem(key,value));\
        click_hashmp_assert(e->item.unshared());\
        e->_hashnext = bucket.list->head;\
\
        bucket.list->head = e;\
\
        p.assign(&e->item);\
\
        _table->_size++;\
        if (likely(mt()))\
            bucket.list.write_end();\
    }\
    if (likely(mt()))\
        _table.read_end();\

template <typename K, typename V, typename Item, typename MT>
inline typename HashContainerMP<K,V,Item,MT>::ptr
HashContainerMP<K,V,Item,MT>::find_insert(const K &key,const V &value) {
    MAKE_FIND_INSERT(ptr,(void),value);
    click_hashmp_assert(p.hasref());
    return p;
}

/**
 *  Replace an item if it exists, insert it otherwise. Do not look at use count !
 *  */
template <typename K, typename V, typename Item, typename MT>
inline void
HashContainerMP<K,V,Item,MT>::insert(const K &key,const V &value, std::function<void(V&value)> on_replace) {
    MAKE_FIND_INSERT(write_ptr,{on_replace(*pprev->item.unprotected_ptr());pprev->item = value;},value);
}

template <typename K, typename V, typename Item, typename MT>
inline typename HashContainerMP<K,V,Item,MT>::ptr
HashContainerMP<K,V,Item,MT>::find_create(const K &key,std::function<V(void)> on_create) {
    MAKE_FIND_INSERT(ptr,(void),on_create());
    return p;
}

template <typename K, typename V, typename Item, typename MT>
inline typename HashContainerMP<K,V,Item,MT>::write_ptr
HashContainerMP<K,V,Item,MT>::find_insert_write(const K &key,const V &value) {
    MAKE_FIND_INSERT(write_ptr,(void),value);
    click_hashmp_assert(p.write_held());
    return p;
}

template <typename K, typename V, typename Item, typename MT>
inline typename HashContainerMP<K,V,Item,MT>::iterator
HashContainerMP<K,V,Item,MT>::begin()
{
    if (likely(mt()))
        _table.read_begin();
    return iterator(this);
}

template <typename K, typename V, typename Item, typename MT>
inline typename HashContainerMP<K,V,Item,MT>::write_iterator
HashContainerMP<K,V,Item,MT>::write_begin()
{
    if (likely(mt()))
        _table.read_begin();
    return write_iterator(this);
}

template <typename K, typename V, typename Item, typename MT>
inline bool
HashContainerMP<K,V,Item,MT>::alone(bool atomic)
{
    if (_table.refcnt() != 0) return false;
    if (likely(mt())) {
    if (atomic)
        _table.write_begin();
    else
        _table.read_begin();
    }
    bool alone = true;
    for (int i = 0; i < _table->nbuckets();i++) {
        Bucket& b = _table->buckets[i];
        if (b.list.refcnt() != 0) {
            alone = false;
            break;
        }
        if (likely(mt())) {
            if (atomic)
                b.list.write_begin();
            else
                b.list.read_begin();
        }
        ListItem* item = b.list->head;
        while (item) {
            if (!item->item.unshared()) {
                alone = false;
                break;
            }
            item = item->_hashnext;
        }
        if (likely(mt())) {
            if (atomic) {
                b.list.write_end();
            } else
                b.list.read_end();
        }
        if (!alone)
            break;
    }
    if (likely(mt())) {
        if (atomic)
            _table.write_end();
        else
            _table.read_end();
    }
    return alone;
}



//_table.read_lock must be held !
template <typename K, typename V, typename Item, typename MT>
inline typename HashContainerMP<K,V,Item,MT>::size_type
HashContainerMP<K,V,Item,MT>::bucket(const K &key)
{
#if HASTABLEMP_POW2_SZ
    return ((size_type) hashcode(key)) & _table->_nbuckets_mask;
#else
    return ((size_type) hashcode(key)) % _table->nbuckets();
#endif
}

template <typename K, typename V, typename Item, typename MT>
inline void HashContainerMP<K,V,Item,MT>::clear()
{
    if (likely(mt()))
        _table.write_begin();
    for (size_type i = 0; i < _table->nbuckets(); ++i) {
         Bucket& b = _table->buckets[i];
         if (likely(mt()))
             b.list.write_begin();
        ListItem* item = b.list->head;
        ListItem* next;
        while (item) {
            next = item->_hashnext;
            erase_item(item);
            item = next;
        }
        b.list->head = 0;
        if (likely(mt()))
            b.list.write_end();
    }
    if (likely(mt()))
        _table.write_end();
    _table->_size = 0;
}


template <typename K, typename V, typename Item, typename MT>
inline bool HashContainerMP<K,V,Item,MT>::contains(const K& key)
{
    if (likely(mt()))
        _table.read_begin();
    Bucket &b = _table->buckets[bucket(key)];
    if (likely(mt()))
        b.list.read_begin();
    if (likely(mt()))
        _table.read_end();
    for (ListItem* list = b.list->head; list; list = list->_hashnext) {
        if (hashkeyeq(hashkey(list), key)) {
            if (likely(mt()))
                b.list.read_end();
            return true;
        }
    }
    if (likely(mt()))
        _table.read_end();

    return false;
}

/*
//These are functions from HashTable still to implement in an MT-way.
//Normally, all needed structures are there.
template <typename K, typename V, typename Item, typename MT=MT_T>
inline typename HashContainerMP<K,V,Item,MT>::iterator
HashContainerMP<K,V,Item,MT>::find_prefer(const K &key)
{
    size_type b = bucket(key);
    T **pprev;
    for (pprev = &_buckets[b]; *pprev; pprev = &_hashnext(*pprev))
    if (_hashkeyeq(_hashkey(*pprev), key)) {
        T *element = *pprev;
        *pprev = _hashnext(element);
        _hashnext(element) = _buckets[b];
        _buckets[b] = element;
        return iterator(this, b, &_buckets[b], element);
    }
    return iterator(this, b, &_buckets[b], 0);
}

template <typename K, typename V, typename Item, typename MT=MT_T>
T *HashContainerMP<K,V,Item,MT>::set(iterator &it, T *element, bool balance)
{
    click_hashmp_assert(it._hc == this && it._bucket < nbuckets());
    click_hashmp_assert(bucket(_hashkey(element)) == it._bucket);
    click_hashmp_assert(!it._element || _hashkeyeq(_hashkey(element), _hashkey(it._element)));
    T *old = it.get();
    if (unlikely(old == element))
    return old;
    if (!element) {
    --_size;
    if (!(*it._pprev = it._element = _hashnext(old)))
        ++it;
    return old;
    }
    if (old)
    _hashnext(element) = _hashnext(old);
    else {
    ++_size;
    if (unlikely(unbalanced()) && balance) {
        rehash(bucket_count() + 1);
        it._bucket = bucket(_hashkey(element));
        it._pprev = &_buckets[it._bucket];
    }
    if (!(_hashnext(element) = *it._pprev))
    }
    *it._pprev = it._element = element;
    return old;
}

template <typename K, typename V, typename Item, typename MT=MT_T>
inline T *HashContainerMP<K,V,Item,MT>::erase(const K &key)
{
    iterator it = find(key);
    return set(it, 0);
}

template <typename K, typename V, typename Item, typename MT=MT_T>
inline void HashContainerMP<K,V,Item,MT>::swap(HashContainerMP<K,V> &o)
{
    HashContainer_rep<T, A> rep(_rep);
    _rep = o._rep;
    o._rep = rep;
}

template <typename K, typename V, typename Item, typename MT=MT_T>
void HashContainerMP<K,V,Item,MT>::rehash(size_type n)
{
    size_type new_nbuckets = 1;
    while (new_nbuckets < n && new_nbuckets < max_bucket_count)
    new_nbuckets = ((new_nbuckets + 1) << 1) - 1;
    click_hashmp_assert(new_nbuckets > 0 && new_nbuckets <= max_bucket_count);
    if (_nbuckets == new_nbuckets)
    return;

    T **new_buckets = (T **) CLICK_LALLOC(sizeof(T *) * new_nbuckets);
    for (size_type b = 0; b < new/; ++b)
    new_buckets[b] = 0;

    size_type old_nbuckets = _nbuckets;
    T **old_buckets = _buckets;
    _nbuckets = new_nbuckets;
    _buckets = new_buckets;

    for (size_t b = 0; b < old_nbuckets; b++)
    for (T *element = old_buckets[b]; element; ) {
        T *next = _hashnext(element);
        size_type new_b = bucket(_hashkey(element));
        _hashnext(element) = new_buckets[new_b];
        new_buckets[new_b] = element;
        element = next;
    }

    CLICK_LFREE(old_buckets, sizeof(T *) * old_nbuckets);
}
*/

template <typename K, typename V>
class HashTableMP : public HashContainerMP<K,V,shared<V> > { public:
    HashTableMP() : HashContainerMP<K,V,shared<V> >::HashContainerMP() {
    }

    HashTableMP(int n) : HashContainerMP<K,V,shared<V> >::HashContainerMP(n) {
    }
};

template <typename K, typename V>
class HashTableH : public HashContainerMP<K,V,not_shared<V>,NOMT > { public:
    HashTableH() : HashContainerMP<K,V,not_shared<V>,NOMT >::HashContainerMP() {
    }

    HashTableH(int n) : HashContainerMP<K,V,not_shared<V>,NOMT >::HashContainerMP(n) {
    }
};


template <typename K, typename V>
class RWHashTableMP : public HashContainerMP<K,V,rwlock<V> > {


};


template <typename K, typename Vin, typename Time = click_jiffies_t, template <typename> class Protector = shared, typename MT=MT_T>
class AgingTableMP {
	struct V {
		Vin value;
		Time time;
	};

  protected:
	HashContainerMP<K,V,Protector<V>,MT > _table;
	Time _timeout;
	typedef typename HashContainerMP<K,V,Protector<V>,MT >::size_type size_type;

  public:
	AgingTableMP(Time timeout = 60) : _table(), _timeout(timeout) {
    }

	void set_timeout(Time timeout) {
		_timeout = timeout;
	}

	/**
	 * Search for a key
	 */
	inline bool find(K key, const Time &now, Vin& storage, const bool update_age = true) {
		return _table.find_clean(key, [&storage,now,update_age](V&c){storage = c.value;if (update_age) c.time = now;}, [now,this](V& v){
                bool remove = v.time + _timeout < now;
#ifdef DEBUG_AGING
                if (remove)
                    click_chatter("Removing entry upon search (time is %d, expired %d, now %d)",v.time,_timeout,now);
#endif
                return remove;
        });
	}

	/**
	 * Insert a key/value
	 */
	inline void insert(const K &key, const Time &now, const Vin& value) {
		const V val = {.value = value, .time = now};
		_table.insert_clean(key, val, [now,this](V& v){
                bool remove = v.time + _timeout < now;
#ifdef DEBUG_AGING
                if (remove)
                    click_chatter("Removing entry upon insertion (time is %d, expired %d, now %d)",v.time,_timeout,now);
#endif
                return remove;
        });
	}

	inline size_type size() {
		return _table.size();
	}
};

template <typename K, typename Vin, typename Time = click_jiffies_t>
class AgingTable : public AgingTableMP<K,Vin,Time,not_shared,NOMT> { public:
	AgingTable(Time timeout = 60 * CLICK_HZ ) : AgingTableMP<K,Vin,Time,not_shared,NOMT>(timeout) {
	}
};
CLICK_ENDDECLS
#endif
