#ifndef CLICK_AGGCOUNTERVECTOR_HH
#define CLICK_AGGCOUNTERVECTOR_HH
#include <click/batchelement.hh>
#include <click/multithread.hh>
#include <click/vector.hh>
#include <click/pair.hh>
#if HAVE_DPDK
#include <rte_mbuf.h>

#if RTE_VERSION >= RTE_VERSION_NUM(22,07,0,0)
#define PKT_RX_FDIR_ID RTE_MBUF_F_RX_FDIR_ID
#endif

#endif
CLICK_DECLS
class HandlerCall;

/*
=c

AggregateCounterVector([I<KEYWORDS>])

=s aggregates

counts packets per aggregate annotation, and keep tracks of a moving average of the same time.

=d

As opposed to AggregateCounter, this element does not use a heap but a vector. It's therefore much faster but needs to know in advance how much elements will be seen.

Keyword arguments are:

=over 8

=item MASK mask to apply to the AGGREGATE annotation. Hence the vector size will be MASK+1, eg the mask is 511 (0x1f) to count 512 elements.

=e

A typical usage is with RSS++, by setting RSS_AGGREGATE to true in FromDPDKDevice, and using a MASK of 511, one will effecitvely count the number of packets per RSS buckets:
FromDPDKDevice(..., RETA_SIZE 512, RSS_AGGREGATE true)
	-> AggregateCounterVectot(MASK 511)
	-> Discard;

=a
AggregateCounter

 */


class AggregateCounterVector : public BatchElement { public:

	/**
	 * The array is composed of _mask+1 Node structures.
	 */
	struct Node {
	    uint64_t count;
	    uint64_t variance;
	    uint32_t epoch;
#if COUNT_FLOWS
	    uint16_t flows; //Max 42*8
	    uint8_t map[42];

	    void add_flow(uint32_t agg) {
		uint16_t n = ((agg >> 24) ^ (agg >> 15)) % (42*8); //Number between 0 and 8*42
		//Set the bit n in map
		if (map[n / 8] & (1 << n % 8)) {

		} else {
			map[n / 8] |= (1 << n % 8);
			flows++;
		}

	    }
#endif
	} CLICK_CACHE_ALIGN;


    AggregateCounterVector() CLICK_COLD;
    ~AggregateCounterVector() CLICK_COLD;

    const char *class_name() const  { return "AggregateCounterVector"; }
    const char *port_count() const	{ return "1/1"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    static String read_handler(Element *e, void *thunk);
    static int write_handler(const String &data, Element *e, void *thunk, ErrorHandler *errh);
    void add_handlers() CLICK_COLD;

    inline Node& find_node(uint32_t agg, const Packet* p, bool &outdated);
    inline Node& find_node_nocheck(uint32_t agg);
    inline void read_values(Vector<uint64_t>& count);
    inline bool update(Packet *);
    inline bool update_batch(PacketBatch *);
    void push(int, Packet *);
    Packet *pull(int);
#if HAVE_BATCH
    void push_batch(int, PacketBatch *) override;
    PacketBatch *pull_batch(int, unsigned) override;
#endif


    inline void advance_epoch() {
	_epoch++;
    }

  private:

    bool _bytes : 1;
    bool _ip_bytes : 1;
    bool _use_packet_count : 1;
    bool _use_extra_length : 1;
    bool _active;
    bool _mark;

    uint32_t _mask;

    uint32_t _epoch;

    Vector<Node> _nodes;



};

/**
 * @pre a is masked
 */
inline AggregateCounterVector::Node&
AggregateCounterVector::find_node(uint32_t a, const Packet* p, bool &outdated)
{
    Node& n = _nodes.unchecked_at(a);
    uint32_t epoch;
#if HAVE_DPDK
    if (_mark) {
        struct rte_mbuf* p_mbuf;
        p_mbuf = (struct rte_mbuf *) p->destructor_argument();

        if (!(p_mbuf->ol_flags & PKT_RX_FDIR_ID))  {
            click_chatter("WARNING : untagged packet");
        }
        epoch = p_mbuf->hash.fdir.hi;
    } else
#endif
    {
        epoch = _epoch;
    }
    if (n.epoch != epoch) {

           // click_chatter("%d now at epoch %d (core %d)", a, epoch, click_current_cpu_id());

        if (epoch < n.epoch) {
            outdated = true;
        } else {
            n.variance = n.variance / 3 + (n.count * 2 / 3);
            n.count = 0;
            n.epoch = epoch;
#if COUNT_FLOWS
            n.flows = 0;
            bzero(n.map,sizeof(n.map));
#endif
        }

    }
    return n;
}

inline AggregateCounterVector::Node&
AggregateCounterVector::find_node_nocheck(uint32_t a)
{
    return _nodes.unchecked_at(a);
}
/*
inline void
AggregateCounterVector::read_values(Vector<Pair<uint64_t,uint64_t> >& count) {
	for (int a ; a < _nodes.size(); a++) {
		count[a].first = _nodes.unchecked_at(a).count;
		count[a].second = _nodes.unchecked_at(a).second / 3 + (count[a].first * 2) / 3;
	}
}
*/
CLICK_ENDDECLS
#endif
