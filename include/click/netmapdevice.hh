#ifndef CLICK_NETMAPDEVICE_HH
#define CLICK_NETMAPDEVICE_HH 1

#if HAVE_NET_NETMAP_H

#include <vector>
#include <list>
#include <map>
#include <click/bitvector.hh>
#include <click/sync.hh>
#include <click/hashmap.hh>
#include <click/multithread.hh>
#include <click/packet.hh>
#include <net/if.h>

#ifndef NETMAP_WITH_LIBS
#define NETMAP_WITH_LIBS 1
#endif

#include <net/netmap.h>
#include <net/netmap_user.h>

CLICK_DECLS

/* a queue of netmap buffers, by index */
class NetmapBufQ {
    unsigned char *buf_start;   /* base address */
    static unsigned int buf_size;
    uint32_t max_index; /* error checking */
    unsigned char *buf_end; /* error checking */

    uint32_t head;  /* index of first buffer */
    uint32_t tail;  /* index of last buffer */

    atomic_uint32_t _count; /* how many ? */

    struct nm_desc* _some_nmd;

    bool shared;

  public:
    NetmapBufQ() : _some_nmd(NULL), shared(false) {
        _count = 0;
    }

    static inline unsigned int buffer_size() {
        return buf_size;
    }

    NetmapBufQ(struct nm_desc* some_nmd) : shared(false) {
        _count = 0;
        init(some_nmd->buf_start,some_nmd->buf_end,some_nmd->some_ring->nr_buf_size);
        _some_nmd = some_nmd;

    }

    ~NetmapBufQ() {
        clear();
    }

    /*
     * Tell the pool to go in single consumer / multiple producer mode
     */
    inline void set_shared() {
       shared = true;
    }

    inline int expand(int n) {
        if (!_some_nmd) return -1;
       click_chatter("Expanding buffer pool with %d new packets",n);
       struct nmbufreq nbr;
       nbr.num = n;
       nbr.head = 0;
       if (ioctl(_some_nmd->fd,NIOCALLOCBUF,&nbr) != 0) {
            click_chatter("Error ! Could not alloc buffers !");
                return -1;
       }

       return insert_all(nbr.head);
    }

    inline void clear() {
            if (_count > 0 && _some_nmd) {
                struct nmbufreq nbr;
                nbr.num = _count;
                nbr.head = head;

                ioctl(_some_nmd->fd,NIOCFREEBUF,&nbr);
            }
            _count = 0;
    };

    inline unsigned int count() const {return _count;};

    inline int insert_all(uint32_t idx) {
        if (unlikely(idx >= max_index || idx == 0)) {
            click_chatter("Error : cannot insert index %d",idx);
             return 0;
        }

        uint32_t firstidx = idx;
        uint32_t *p;
        while (idx > 0) {
            p = reinterpret_cast<uint32_t*>(buf_start +
                                idx * buf_size);
            idx = *p;
            _count++;
        }

        do {
            *p = head;
            if (!shared)
                head = firstidx;
        } while (shared && !__sync_bool_compare_and_swap(&head, *p, firstidx));

        return _count;
    }

    inline unsigned int insert(unsigned char* buf) {
        return insert((buf - buf_start) / buf_size);
    }

    inline unsigned int insert(uint32_t idx) {
    assert(idx > 0 && idx < max_index);

    uint32_t *p = reinterpret_cast<uint32_t *>(buf_start +
        idx * buf_size);
    // prepend
    do {
        *p = head;
        /*if (head == 0) {
            tail = idx;
        }*/
        if (unlikely(shared)) {
            if (__sync_bool_compare_and_swap(&head, *p, idx)) {
                _count++;
                return 0;
            }

        } else {
            head = idx;
            (*((uint32_t*)&_count))++;
            return 0;
        }
    } while (1);
    return 0;
    }

    inline unsigned int insert_p(unsigned char *p) {
//        assert (p >= buf_start && p < buf_end);
        return insert((p - buf_start) / buf_size);
    }

    inline uint32_t extract() {

        if (_count <= 0) {
            expand(1024);
            if (_count == 0) return 0;
        }
        uint32_t idx;
        uint32_t *p;
        do {
            idx = head;


            p  = reinterpret_cast<uint32_t *>(buf_start +
                idx * buf_size);

            if (unlikely(shared)) {
                if (__sync_bool_compare_and_swap(&head,idx,*p)) {
                    _count--;
                    break;
                }

            } else {
                head = *p;
                (*((uint32_t*)&_count))--;
                break;
            }

        } while(1);
        return idx;
    }

    inline unsigned char * extract_p() {
        uint32_t idx = extract();
    return (idx == 0) ? 0 : buf_start + idx * buf_size;
    }

    inline int init (void *beg, void *end, uint32_t _size) {
    click_chatter("Initializing NetmapBufQ %p size %d mem %p %p\n",
        this, _size, beg, end);
    head = tail = max_index = 0;
    _count = 0;
    buf_size = 0;
    buf_start = buf_end = 0;
    if (_size == 0 || _size > 0x10000 ||
        beg == 0 || end == 0 || end < beg) {
        click_chatter("NetmapBufQ %p bad args: size %d mem %p %p\n",
        this, _size, beg, end);
        return 1;
    }

    buf_size = _size;
    buf_start = reinterpret_cast<unsigned char *>(beg);
    buf_end = reinterpret_cast<unsigned char *>(end);
    max_index = (buf_end - buf_start) / buf_size;
    // check max_index overflow ?
    return 0;
    }

    static bool is_netmap_packet(Packet* p) {

#if !HAVE_NETMAP_PACKET_POOL&&!CLICK_DPDK_POOLS
        return (p->buffer_destructor() == buffer_destructor);
#else
        return false;
#endif
    }

    static void buffer_destructor(unsigned char *buf, size_t, void *arg) {
        ((NetmapBufQ*)arg)->insert_p(buf);
    }

    static void buffer_destructor_fake(unsigned char *, size_t, void *) {
    }

    static NetmapBufQ* local_pool() {
    	return NetmapBufQ::netmap_buf_pools.get();
    }

    static int initialize(struct nm_desc* some_nmd) {
    	if (netmap_buf_pools.size() < (unsigned)click_nthreads)
			netmap_buf_pools.resize(click_nthreads,NULL);
		for (int i = 0; i < click_nthreads; i++) {
			if (netmap_buf_pools.get_value(i) == NULL) {
				NetmapBufQ* q = new NetmapBufQ(some_nmd);
				if (q->expand(1024) <= 0)
					return -1;
				netmap_buf_pools.set_value(i,q);
			}
		}
		return 0;
    }

    static void cleanup() {
		for (unsigned int i = 0; i < netmap_buf_pools.size(); i++) {
			if (netmap_buf_pools.get_value(i) && netmap_buf_pools.get_value(i)->count() > 0) {
				click_chatter("Free pool %d (have %d packets)",i,netmap_buf_pools.get_value(i)->count());
				delete netmap_buf_pools.get_value(i);
				netmap_buf_pools.set_value(i, NULL);
			} else {
				click_chatter("No free because no packet pool %d %d",netmap_buf_pools.get_value(i),netmap_buf_pools.get_value(i)->count());
			}
		}
    }

    inline static NetmapBufQ*& get_local_pool() {
    	return (netmap_buf_pools.get());
    }

    inline static NetmapBufQ*& get_local_pool(int tid) {
        return (netmap_buf_pools.get_value_for_thread(tid));
    }

private :
    static per_thread<NetmapBufQ*> netmap_buf_pools;

}  __attribute__((aligned(64)));

class NetmapDevice;

/**
 * A Netmap interface, its global descriptor and one descriptor per queue
 */
class NetmapDevice {
	public:

	int _minfd;
	int _maxfd;

	NetmapDevice(String ifname) CLICK_COLD;
	~NetmapDevice() CLICK_COLD;

	atomic_uint32_t n_refs;

	struct nm_desc* parent_nmd;
	std::vector<struct nm_desc*> nmds;

	String ifname;
	int n_queues;

	void destroy() {
	    --_use_count;
	    if (_use_count == 0)
	        delete this;
	}

	static NetmapDevice* open(String ifname) {
	    NetmapDevice* d = nics.find(ifname);
	    if (d == NULL) {
	        d = new NetmapDevice(ifname);
	        if (d->initialize() != 0) {
	            return NULL;
	        }
	        nics.insert(ifname,d);
	    }
	    d->_use_count++;
	    return d;
	}

	static struct nm_desc* some_nmd;

protected :

	static HashMap<String,NetmapDevice*> nics;

private:
	int _use_count;

	int initialize();
};

CLICK_ENDDECLS
#endif
#endif
