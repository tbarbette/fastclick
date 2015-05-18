#ifndef CLICK_NETMAPINFO_HH
#define CLICK_NETMAPINFO_HH 1

#include <click/netmapdevice.hh>

#if HAVE_NET_NETMAP_H
#include <net/if.h>

#ifndef NETMAP_WITH_LIBS
#define NETMAP_WITH_LIBS 1
#endif

#include <net/netmap.h>
#include <net/netmap_user.h>


// XXX bug in netmap_user.h , the prototype should be available

#ifndef NETMAP_WITH_LIBS
typedef void (*nm_cb_t)(u_char *, const struct nm_pkthdr *, const u_char *d);
#endif

#include <click/packet.hh>
#include <click/error.hh>
CLICK_DECLS

/* a netmap port as returned by nm_open */
class NetmapInfo { public:

	struct nm_desc *desc;
	class NetmapInfo *parent;	/* same pool */
	class NetmapBufQ bufq;		/* free buffer queue */

	// to recycle buffers,
	// nmr.arg3 is the number of extra buffers
	// nifp->ni_bufs_head is the index of the first buffer.
	unsigned int active_users; // we do not close until done.

	NetmapInfo *destructor_arg;	// either this or parent's main_mem

	int open(const String &ifname,
		 bool always_error, ErrorHandler *errh);
	void initialize_rings_rx(int timestamp);
	void initialize_rings_tx();
	void close(int fd);
	// send a packet, possibly using zerocopy if noutputs == 0
	// and other conditions apply
        bool send_packet(Packet *p, int noutputs);

	int dispatch(int burst, nm_cb_t cb, u_char *arg);

#if 0
	// XXX return a buffer to the ring
	bool refill(struct netmap_ring *ring) {
	    if (buffers) {
		unsigned char *buf = buffers;
		buffers = *reinterpret_cast<unsigned char **>(buffers);
		unsigned res1idx = ring->head;
		ring->slot[res1idx].buf_idx = NETMAP_BUF_IDX(ring, (char *) buf);
		ring->slot[res1idx].flags |= NS_BUF_CHANGED;
		ring->head = nm_ring_next(ring, res1idx);
		return true;
	    } else
		return false;
	}
#endif

    static bool is_netmap_buffer(Packet *p) {
	return p->buffer_destructor() == buffer_destructor;
    }

    /*
     * the destructor appends the buffer to the freelist in the ring,
     * using the first field as pointer.
     */
    static void buffer_destructor(unsigned char *buf, size_t, void *arg) {
	NetmapInfo *x = reinterpret_cast<NetmapInfo *>(arg);
	click_chatter("%s ni %p buf %p\n", __FUNCTION__,
		x, buf);
	if (x->bufq.insert_p(buf)) {
		// XXX error
	}
    }
};

CLICK_ENDDECLS
#endif // HAVE_NETMAP_H

#endif
