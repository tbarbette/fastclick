#ifndef CLICK_HashContainerMP_HH
#define CLICK_HashContainerMP_HH
#include <click/glue.hh>
#include <click/hashcode.hh>
#define CLICK_DEBUG_HASHMAP 1
#include <click/allocator.hh>
#include <functional>
#include <click/multithread.hh>
#if CLICK_DEBUG_HASHMAP
# define click_hashmp_assert(x) assert(x)
#else
# define click_hashmp_assert(x)
#endif
CLICK_DECLS


/** @class HashContainerMP
  @brief Intrusive hash table template, MT safe version.

  K is the type of the key
  V is the type of the value
  Item is the value type enclosed by a reference counting mechanism based
   on shared pointers like shared<V> or rw_shared<V>, depending on the
   level of protection you want.

  For convenience HashTableMP is an alias for HashContainerMP<K,V,shared<V>>
*/
template <typename K, typename V, typename Item>
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
        Table() : _nbuckets(0), buckets(0) {
            _size = 0;
        }
        size_t _nbuckets;
        Bucket* buckets;
        atomic_uint32_t _size;
    private:
        Table(const Table &o) {};
        void operator=(const Table &o){};
    };

    typedef typename Item::ptr ptr;
    typedef typename Item::write_ptr write_ptr;

    class iterator {
        mutable HashContainerMP<K,V,Item>* _h;
        int _b;
        ListItem* _prev;
        ListItem* _item;
    public:
        ~iterator() {
            if (_h) {
                if (_b < _h->_table->_nbuckets) {
                    if (likely(_h->_mt))
                        _h->_table->buckets[_b].list.read_end();
                }
                if (likely(_h->_mt))
                    _h->_table.read_end();
            }
        }

        /** @brief Advance this iterator to the next element. */
        void operator++() {
            if (_b == _h->_table->_nbuckets) return;
            if (_item) {
                _item = _item->_hashnext;
                _prev = _item;
            }
            while (!_item) {
                //click_chatter("Bucket %d : %p",_b,_h->_table->buckets[_b]);
                if (likely(_h->_mt))
                    _h->_table->buckets[_b].list.read_end();
                if (++_b == _h->_table->_nbuckets) return;
                if (likely(_h->_mt))
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
        iterator(HashContainerMP<K,V,Item>* h) : _h(h), _prev(0) {
            _b = 0;
            if (likely(_h->_mt))
                _h->_table->buckets[_b].list.read_begin();
            _item =  _h->_table->buckets[_b].list->head;
            if (_item == 0) {
                (*this)++;
            }
        }
        friend class HashContainerMP<K,V,Item>;
    };

    class write_iterator {
        mutable HashContainerMP<K,V,Item>* _h;
        int _b;
        ListItem* _prev;
        ListItem* _item;
    public:
        ~write_iterator() {
            if (_h) {
                if (_b < _h->_table->_nbuckets) {
                    if (likely(_h->_mt))
                        _h->_table->buckets[_b].list.write_end();
                }
                if (likely(_h->_mt))
                    _h->_table.read_end();
            }
            _h = 0; //Prevent read destruction
        }

        /** @brief Advance this iterator to the next element. */
        void operator++() {
            if (_b == _h->_table->_nbuckets) return;
            if (_item) {
                _item = _item->_hashnext;
                _prev = _item;
            }
            while (!_item) {
                if (likely(_h->_mt))
                    _h->_table->buckets[_b].list.write_end();
                if (++_b == _h->_table->_nbuckets) return;
                if (likely(_h->_mt))
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
        write_iterator(HashContainerMP<K,V,Item>* h) : _h(h), _prev(0) {
            _b = 0;
            if (likely(h->_mt))
                _h->_table->buckets[_b].list.write_begin();
            _item =  _h->_table->buckets[_b].list->head;
            if (_item == 0) {
                (*this)++;
            }
        }
        friend class HashContainerMP<K,V,Item>;
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

    typedef size_t size_type;

    enum {
#if CLICK_LINUXMODULE
    max_bucket_count = 4194303,
#else
    max_bucket_count = (size_t) -1,
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
        if (likely(_mt))
            _table.read_begin();
        s = _table->_size;
        if (likely(_mt))
            _table.read_end();
        return s;
    }

    /** @brief Return the number of buckets. */
    inline size_type buckets() {
        size_type s;
        if (likely(_mt))
            _table.read_begin();
        s = _table->_nbuckets;
        if (likely(_mt))
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
        if (likely(_mt))
            _table.read_begin();
        r = _table->_size > 2 * _table->_nbuckets && _table->_nbuckets < max_bucket_count;
        if (likely(_mt))
            _table.read_end();
        return r;
    }

    inline iterator begin();

    inline write_iterator write_begin();

    /** @brief Test if an element with key @a key exists in the table. */
    inline bool contains(const K& key);

    /** @brief Return a read pointer for the element with @a key, if any. */
    inline ptr find(const K &key);

    /** @brief Return a write pointer for the element with @a key, if any. */
    inline write_ptr find_write(const K &key);

    /** @brief Copy the value of V for K in storage if K is found, delete it from the table and return true. If nonfound, nothing is deleted and return false. */
    inline bool find_remove(const K &key, V &storage);

    inline bool find_remove_clean(const K &key, std::function<void(V &storage)> store, std::function<bool(V &storage)> shouldremove);

    inline ptr find_insert(const K &key, const V &value);

    /** @brief Replace an item if it exists, insert it otherwise. Do not look at use count ! */
    inline void replace(const K &key, const V &value, std::function<void(V&value)> on_replace);

    inline write_ptr find_insert_write(const K &key, const V &value);

    inline void set(const K &key, const V &value);

    inline void erase(const K &key);

    void clear();

    void rehash(size_type n);

    inline bool alone(bool atomic = false);


    uint32_t refcnt() {
        return _table.refcnt();
    }

    inline void disable_mt() {
        _mt = false;
    }
  protected:
    bool _mt;

    __rwlock<Table> _table;
    per_thread<ListItem*> _pending_release;

    void initialize(size_type n);
    void deinitialize();

    HashContainerMP(const HashContainerMP<K,V,Item> &);
    HashContainerMP<K,V,Item> &operator=(const HashContainerMP<K,V,Item> &);

    //_table read lock must be held
    size_type bucket(const K &key);

    //Write lock of the bucket must be held, and the item be out of the structure
    void erase_item(ListItem* item) {
        if (item->item.refcnt() == 0) {
            _allocator.release(item);
        } else {
            item->_hashnext = _pending_release.get();
            _pending_release.set(item);
        }
    }

    ListItem* allocate(const ListItem &li) {
        ListItem* it;
        if ((it = _pending_release.get()) && it->item.refcnt() == 0) {
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

    friend class iterator;

};

template <typename K, typename V, typename Item>
void HashContainerMP<K,V,Item>::initialize(size_type n)
{
    _table->_size = 0;
    _table->_nbuckets = n;
    _table->buckets = (Bucket *) CLICK_LALLOC(sizeof(Bucket) * _table->_nbuckets);
    for (size_type b = 0; b < _table->_nbuckets; ++b)
            new(&_table->buckets[b]) Bucket();
}

template <typename K, typename V, typename Item>
void HashContainerMP<K,V,Item>::deinitialize()
{
    for (size_type b = 0; b < _table->_nbuckets; ++b)
        _table->buckets[b].~Bucket();
    CLICK_LFREE(_table->buckets, sizeof(Bucket) * _table->_nbuckets);
}


template <typename K, typename V, typename Item>
HashContainerMP<K,V,Item>::HashContainerMP() :
  _mt(true), _table(), _pending_release(0)
{
    initialize(initial_bucket_count);
}

template <typename K, typename V, typename Item>
HashContainerMP<K,V,Item>::HashContainerMP(size_type nb) :
     _mt(true), _table(), _pending_release(0)
{
    size_type b = 1;
    while (b < nb && b < max_bucket_count)
    b = ((b + 1) << 1) - 1;

    initialize(b);
}

template <typename K, typename V, typename Item>
HashContainerMP<K,V,Item>::~HashContainerMP()
{
    clear();
    release_pending(true);
    deinitialize();
}
#define MAKE_FIND(ptr_type) \
        if (likely(_mt))\
            _table.read_begin();\
        size_type b = bucket(key);\
        Bucket& bucket = _table->buckets[b];\
        if (likely(_mt))\
            bucket.list.read_begin();\
        if (likely(_mt))\
            _table.read_end();\
        ListItem *pprev;\
        ptr_type p;\
        for (pprev = bucket.list->head; pprev; pprev = hashnext(pprev))\
        if (hashkeyeq(hashkey(pprev), key)) {\
            p.assign(&pprev->item);\
            break;\
        }\
        if (likely(_mt))\
            bucket.list.read_end();\

template <typename K, typename V, typename Item>
inline typename HashContainerMP<K,V,Item>::ptr
HashContainerMP<K,V,Item>::find(const K &key)
{
    MAKE_FIND(ptr);
    return p;
}

template <typename K, typename V, typename Item>
inline typename HashContainerMP<K,V,Item>::write_ptr
HashContainerMP<K,V,Item>::find_write(const K &key)
{
    MAKE_FIND(write_ptr);
    return p;
}

template <typename K, typename V, typename Item>
inline bool
HashContainerMP<K,V,Item>::find_remove_clean(const K &key, std::function<void(V &c)> store, std::function<bool(V &c)> shoulddelete)
{
    if (likely(_mt))
        _table.read_begin();
    size_type b = bucket(key);
    Bucket& bucket = _table->buckets[b];
    if (likely(_mt))
        bucket.list.write_begin();
    if (likely(_mt))
        _table.read_end();
    ListItem *pprev = bucket.list->head;
    ListItem* *pprev_ptr = &bucket.list->head;
    ptr p;
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
                *pprev_ptr = pprev->_hashnext;
                erase_item(pprev);
                goto found;
            }
            pprev_ptr = &pprev->_hashnext;
            pprev = pprev->_hashnext;
        }
    }
    if (likely(_mt))
        bucket.list.write_end();
    return false;
    found:
    _table->_size--;
    if (likely(_mt))
        bucket.list.write_end();
    return true;
}

template <typename K, typename V, typename Item>
inline bool
HashContainerMP<K,V,Item>::find_remove(const K &key, V &storage) {
    return find_remove_clean(key, [&storage](V&c){storage = c;}, [](V&){return false;});
}


template <typename K, typename V, typename Item>
inline void
HashContainerMP<K,V,Item>::erase(const K &key) {
    find_remove_clean(key, [](V&){}, [](V&){return false;});
}

#define MAKE_FIND_INSERT(ptr_type,on_exists) \
    if (likely(_mt))\
        _table.read_begin();\
    size_type b = bucket(key);\
    Bucket& bucket = _table->buckets[b];\
\
retry:\
    if (likely(_mt))\
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
        if (likely(_mt))\
            bucket.list.read_end();\
    } else {\
        if (likely(_mt) && !bucket.list.read_to_write()) {\
            goto retry;\
        }\
        ListItem* e = allocate(ListItem(key,value));\
        click_hashmp_assert(e->item.refcnt() == 0);\
        e->_hashnext = bucket.list->head;\
\
        bucket.list->head = e;\
\
        p.assign(&e->item);\
\
        _table->_size++;\
        if (likely(_mt))\
            bucket.list.write_end();\
    }\
    if (likely(_mt))\
        _table.read_end();\

template <typename K, typename V, typename Item>
inline typename HashContainerMP<K,V,Item>::ptr
HashContainerMP<K,V,Item>::find_insert(const K &key,const V &value) {
    MAKE_FIND_INSERT(ptr,(void));
    click_hashmp_assert(p.refcnt() > 0);
    return p;
}

template <typename K, typename V, typename Item>
inline void
HashContainerMP<K,V,Item>::replace(const K &key,const V &value, std::function<void(V&value)> on_replace) {
    MAKE_FIND_INSERT(ptr,{on_replace(*pprev->item.unprotected_ptr());pprev->item = value;});
}

template <typename K, typename V, typename Item>
inline typename HashContainerMP<K,V,Item>::write_ptr
HashContainerMP<K,V,Item>::find_insert_write(const K &key,const V &value) {
    MAKE_FIND_INSERT(write_ptr,(void));
    click_hashmp_assert(p.refcnt() == -1);
    return p;
}

template <typename K, typename V, typename Item>
inline void
HashContainerMP<K,V,Item>::set(const K &key,const V &value) {
    MAKE_FIND_INSERT(write_ptr,{*p = value;});
}


template <typename K, typename V, typename Item>
inline typename HashContainerMP<K,V,Item>::iterator
HashContainerMP<K,V,Item>::begin()
{
    if (likely(_mt))
        _table.read_begin();
    return iterator(this);
}

template <typename K, typename V, typename Item>
inline typename HashContainerMP<K,V,Item>::write_iterator
HashContainerMP<K,V,Item>::write_begin()
{
    if (likely(_mt))
        _table.read_begin();
    return write_iterator(this);
}

template <typename K, typename V, typename Item>
inline bool
HashContainerMP<K,V,Item>::alone(bool atomic)
{
    if (_table.refcnt() != 0) return false;
    if (likely(_mt)) {
    if (atomic)
        _table.write_begin();
    else
        _table.read_begin();
    }
    bool alone = true;
    for (int i = 0; i < _table->_nbuckets;i++) {
        Bucket& b = _table->buckets[i];
        if (b.list.refcnt() != 0) {
            alone = false;
            break;
        }
        if (likely(_mt)) {
            if (atomic)
                b.list.write_begin();
            else
                b.list.read_begin();
        }
        ListItem* item = b.list->head;
        while (item) {
            if (item->item.refcnt() != 0) {
                alone = false;
                break;
            }
            item = item->_hashnext;
        }
        if (likely(_mt)) {
            if (atomic) {
                b.list.write_end();
            } else
                b.list.read_end();
        }
        if (!alone)
            break;
    }
    if (likely(_mt)) {
        if (atomic)
            _table.write_end();
        else
            _table.read_end();
    }
    return alone;
}



//_table.read_lock must be held !
template <typename K, typename V, typename Item>
inline typename HashContainerMP<K,V,Item>::size_type
HashContainerMP<K,V,Item>::bucket(const K &key)
{
    return ((size_type) hashcode(key)) % _table->_nbuckets;
}

template <typename K, typename V, typename Item>
inline void HashContainerMP<K,V,Item>::clear()
{
    if (likely(_mt))
        _table.write_begin();
    for (size_type i = 0; i < _table->_nbuckets; ++i) {
         Bucket& b = _table->buckets[i];
         if (likely(_mt))
             b.list.write_begin();
        ListItem* item = b.list->head;
        ListItem* next;
        while (item) {
            next = item->_hashnext;
            erase_item(item);
            item = next;
        }
        b.list->head = 0;
        if (likely(_mt))
            b.list.write_end();
    }
    if (likely(_mt))
        _table.write_end();
    _table->_size = 0;
}


template <typename K, typename V, typename Item>
inline bool HashContainerMP<K,V,Item>::contains(const K& key)
{
    if (likely(_mt))
        _table.read_begin();
    Bucket &b = _table->buckets[bucket(key)];
    if (likely(_mt))
        b.list.read_begin();
    if (likely(_mt))
        _table.read_end();
    for (ListItem* list = b.list->head; list; list = list->_hashnext) {
        if (hashkeyeq(hashkey(list), key)) {
            if (likely(_mt))
                b.list.read_end();
            return true;
        }
    }
    if (likely(_mt))
        _table.read_end();

    return false;
}

/*
//These are functions from HashTable still to implement in an MT-way.
//Normally, all needed structures are there.
template <typename K, typename V, typename Item>
inline typename HashContainerMP<K,V,Item>::iterator
HashContainerMP<K,V,Item>::find_prefer(const K &key)
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

template <typename K, typename V, typename Item>
T *HashContainerMP<K,V,Item>::set(iterator &it, T *element, bool balance)
{
    click_hashmp_assert(it._hc == this && it._bucket < _nbuckets);
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

template <typename K, typename V, typename Item>
inline T *HashContainerMP<K,V,Item>::erase(const K &key)
{
    iterator it = find(key);
    return set(it, 0);
}

template <typename K, typename V, typename Item>
inline void HashContainerMP<K,V,Item>::swap(HashContainerMP<K,V> &o)
{
    HashContainer_rep<T, A> rep(_rep);
    _rep = o._rep;
    o._rep = rep;
}

template <typename K, typename V, typename Item>
void HashContainerMP<K,V,Item>::rehash(size_type n)
{
    size_type new_nbuckets = 1;
    while (new_nbuckets < n && new_nbuckets < max_bucket_count)
    new_nbuckets = ((new_nbuckets + 1) << 1) - 1;
    click_hashmp_assert(new_nbuckets > 0 && new_nbuckets <= max_bucket_count);
    if (_nbuckets == new_nbuckets)
    return;

    T **new_buckets = (T **) CLICK_LALLOC(sizeof(T *) * new_nbuckets);
    for (size_type b = 0; b < new_nbuckets; ++b)
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
class RWHashTableMP : public HashContainerMP<K,V,rwlock<V> > {


};

CLICK_ENDDECLS
#endif
