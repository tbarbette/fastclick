#ifndef CLICK_AGGCOUNTER_HH
#define CLICK_AGGCOUNTER_HH
#include <click/batchelement.hh>
#include <click/multithread.hh>
CLICK_DECLS
class HandlerCall;

/*
=c

AggregateCounter([I<KEYWORDS>])

=s aggregates

counts packets per aggregate annotation

=d

AggregateCounter maintains counts of how many packets or bytes it has seen for
each aggregate value. Each aggregate annotation value gets a different count.
Call its C<write_file> or C<write_text_file> write handler to get a dump of
the information.

The C<freeze> handler, and the C<AGGREGATE_FREEZE> and C<COUNT_FREEZE>
keyword arguments, can put AggregateCounter in a frozen state. Frozen
AggregateCounters only update existing counters; they do not create new
counters for previously unseen aggregate values.

AggregateCounter may have one or two inputs. The optional second input is
always frozen. (It is only useful when the element is push.) It may also have
two outputs. If so, and the element is push, then packets that were counted
are emitted on the first output, while other packets are emitted on the second
output.

Keyword arguments are:

=over 8

=item BYTES

Boolean. If true, then count bytes, not packets. Default is false.

=item IP_BYTES

Boolean. If true, then do not count bytes from the link header. Default is
false.

=item MULTIPACKET

Boolean. If true, and BYTES is false, then use packets' packet count
annotations to add to the number of packets seen. Elements like
FromIPSummaryDump set this annotation. Default is true.

=item EXTRA_LENGTH

Boolean. If true, and BYTES is true, then include packets' extra length
annotations in the byte counts. Elements like FromDump set this annotation.
Default is true.

=item AGGREGATE_STOP

Unsigned. Stop the router once I<N> distinct aggregates have been seen.
Default is never to stop.

=item AGGREGATE_FREEZE

Unsigned. Freeze the AggregateCounter once I<N> distinct aggregates have been
seen. Default is never to freeze.

=item AGGREGATE_CALL

Argument is 'I<N> I<HANDLER> [I<VALUE>]'. Call the given write handler, with
the supplied value, once I<N> distinct aggregates have been seen.

The three AGGREGATE keywords are mutually exclusive. Supply at most one of
them.

=item COUNT_STOP

Unsigned. Stop the router once the total count (of bytes or packets) has
reached or exceeded I<N>. Default is never to stop.

=item COUNT_FREEZE

Unsigned. Freeze the AggregateCounter once the total count has reached or
exceeded I<N>. Default is never to freeze.

=item COUNT_CALL

Argument is 'I<N> I<HANDLER> [I<VALUE>]'. Call the given write handler, with
the supplied value, once the total count has reached or exceeded I<N>.

The three COUNT keywords are mutually exclusive. Supply at most one of
them.

=item BANNER

String. This banner is written to the head of any output file. It should
probably begin with a comment character, like '!' or '#'. Default is empty.

=back

=h write_file write-only

Argument is a filename, or 'C<->', meaning standard out. Write a packed binary
file containing all current data to the specified filename. The format is a
couple text lines, followed by a line containing 'C<!packed_le>' or
'C<!packed_be>', followed by N 8-byte records. In each record, bytes 1-4 are
the aggregate, and bytes 5-8 are the count. Both values are 32-bit integers.
The byte order is indicated by the 'C<!packed>' line: 'C<!packed_le>' means
little-endian, 'C<!packed_be>' means big-endian.

=h write_text_file write-only

Argument is a filename, or 'C<->', meaning standard out. Write a text file
containing all current data to the specified filename. The format is a couple
text lines, followed by N data lines, each containing the aggregate ID in
decimal, a space, then the count in decimal.

=h write_ip_file write-only

Argument is a filename, or 'C<->', meaning standard out. Write a text file
containing all current data to the specified filename. The format is as in
C<write_text_file>, except that aggregate IDs are printed as IP addresses.

=h freeze read/write

Returns or sets the AggregateCounter's frozen state, which is 'true' or
'false'. AggregateCounter starts off unfrozen.

=h active read/write

Returns or sets the AggregateCounter's active state. When AggregateCounter is
inactive ('false'), it does not record information about any packets that
pass. It starts out active.

=h stop write-only

When any value is written to this handler, AggregateCounter sets 'active' to
false and additionally stops the driver.

=h counts_pdf write-only

When any value is written to this handler, AggregateCounter will recalculate
its counters. The new aggregate identifiers equal the old counts; the new
counts represent how many times each old count appeared. The old aggregate
identifiers are thrown away. To put it another way, AggregateCounter creates a
multiset containing all aggregate counts, then stores each count as an
aggregate, with its number of occurrences in the multiset as its count.

=h banner read/write

Returns or sets the BANNER setting.

=h aggregate_call read/write

Returns or sets the AGGREGATE_CALL setting.

=h count_call read/write

Returns or sets the COUNT_CALL setting.

=h nagg read-only

Returns the number of aggregates that have been seen so far.

=n

The aggregate identifier is stored in host byte order. Thus, the aggregate ID
corresponding to IP address 128.0.0.0 is 2147483648.

Only available in user-level processes.

=e

This configuration reads an IP summary dump in from standard input, aggregates
based on destination IP address, and counts packets. When the dump is done,
Click will write the aggregate counter's data to standard output, in text
form.

  FromIPSummaryDump(-, STOP true)
	-> AggregateIP(ip dst)
	-> ac :: AggregateCounter
	-> Discard;

  DriverManager(wait_pause,
	write ac.write_text_file -);

Will output something like:

    !IPAggregate 1.0
    !num_nonzero 15
    2342847313 4
    2342848755 10
    2342855240 6
    2342855720 1
    2342856146 6
    2342859764 1
    2342862860 1
    2342865100 1
    2342867259 1
    2342868704 3
    2342871589 7
    2342873863 15
    2342878686 2
    2342889502 2
    2342895905 5

=a

AggregateIP, AggregatePacketCounter, FromIPSummaryDump, FromDump */

struct Node {
    uint32_t aggregate;
    uint32_t count;
    Node *child[2];
};

struct AggregateCounterState {
    Node* root;
    Node* free;
    Vector<Node*>  blocks;
    uint64_t count;
    AggregateCounterState() : root(0), free(0), blocks(), count(0) {

    }
};

template <typename T>
class AggregateCounterBase : public BatchElement { public:

    AggregateCounterBase() CLICK_COLD;
    ~AggregateCounterBase() CLICK_COLD;

    const char *port_count() const override	{ return "1-2/1-2"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    inline bool update(Packet *, bool frozen = false);
    inline bool update_batch(PacketBatch *, bool frozen = false);
    void push(int, Packet *);
    Packet *pull(int);
#if HAVE_BATCH
    void push_batch(int, PacketBatch *) override;
    PacketBatch *pull_batch(int, unsigned) override;
#endif

    bool empty() const			{ return _num_nonzero == 0; }
    int clear(AggregateCounterState &s, ErrorHandler * = 0);
    enum WriteFormat { WR_TEXT = 0, WR_BINARY = 1, WR_TEXT_IP = 2, WR_TEXT_PDF = 3 };
    int write_file(String, WriteFormat, ErrorHandler *) const;
    void reaggregate_counts();

  private:

    bool _bytes : 1;
    bool _ip_bytes : 1;
    bool _use_packet_count : 1;
    bool _use_extra_length : 1;
    bool _frozen;
    bool _active;

    T _state;
    uint32_t _num_nonzero;
    uint32_t _mask;

    uint32_t _call_nnz;
    HandlerCall *_call_nnz_h;
    uint64_t _call_count;
    HandlerCall *_call_count_h;

    String _output_banner;

    Node *new_node(AggregateCounterState &s);
    Node *new_node_block(AggregateCounterState &s);
    void free_node(AggregateCounterState &s, Node *);

    Node *make_peer(uint32_t, Node *, bool frozen);
    Node *find_node(uint32_t, bool frozen = false);
    void reaggregate_node(AggregateCounterState &s, Node *);
    void clear_node(AggregateCounterState &s, Node *);

    static void write_batch(FILE *f, WriteFormat format, uint32_t *buffer, int pos, double count, ErrorHandler *);
    void write_nodes(const Node *, FILE *, WriteFormat, uint32_t *, int &, int, ErrorHandler *) const;
    static int write_file_handler(const String &, Element *, void *, ErrorHandler *);
    static String read_handler(Element *, void *) CLICK_COLD;
    static int write_handler(const String &, Element *, void *, ErrorHandler *) CLICK_COLD;

};
class AggregateCounter : public AggregateCounterBase<not_per_thread<AggregateCounterState> > { public:
    const char *class_name() const override  { return "AggregateCounter"; }
};

class AggregateCounterIMP : public AggregateCounterBase<per_thread<AggregateCounterState> > { public:
    const char *class_name() const override  { return "AggregateCounterIMP"; }
};


template <typename T>
Node *
AggregateCounterBase<T>::new_node(AggregateCounterState &s)
{
    if (s.free) {
	Node *n = s.free;
	s.free = n->child[0];
	return n;
    } else
	return new_node_block(s);
}

template <typename T> inline void
AggregateCounterBase<T>::free_node(AggregateCounterState &s, Node *n)
{
    n->child[0] = s.free;
    s.free = n;
}

CLICK_ENDDECLS
#endif
