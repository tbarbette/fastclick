#ifndef CLICK_IPFILTER_HH
#define CLICK_IPFILTER_HH
#include "elements/standard/classification.hh"
#include <click/batchelement.hh>
#include <click/ipflowid.hh>
#include <click/error.hh>
CLICK_DECLS

/*
=c

IPFilter([CACHING,] ACTION_1 PATTERN_1, ..., ACTION_N PATTERN_N)

=s ip

filters IP packets by contents

=d

Filters IP packets. IPFilter can have an arbitrary number of filters, which
are ACTION-PATTERN pairs. The ACTIONs describe what to do with packets,
while the PATTERNs are tcpdump(1)-like patterns; see IPClassifier(n) for a
description of their syntax. Packets are tested against the filters in
order, and are processed according to the ACTION in the first filter that
matched.

Each ACTION is either a port number, which specifies that the packet should be
sent out on that port; 'C<allow>', which is equivalent to 'C<0>'; or 'C<drop>'
, which means drop the packet. You can also say 'C<deny>' instead of
'C<drop>'.

One can load rules from a file with the "file" ACTION and the path as PATTERN.
E.g., where firewall.rules is a file with one rule per line following the
ACTION-PATTERN described above:
  IPFilter(file firewall.rules);


The IPFilter element has an arbitrary number of outputs. Input packets must
have their IP header annotation set; CheckIPHeader and MarkIPHeader do
this.

Arguments:

=item CACHING

Boolean. Enables or disables caching. Defaults to false (i.e., no caching).

=n

Every IPFilter element has an equivalent corresponding IPClassifier element
and vice versa. Use the element whose syntax is more convenient for your
needs.

=e

This large IPFilter implements the incoming packet filtering rules for the
"Interior router" described on pp691-692 of I<Building Internet Firewalls,
Second Edition> (Elizabeth D. Zwicky, Simon Cooper, and D. Brent Chapman,
O'Reilly and Associates, 2000). The captialized words (C<INTERNALNET>,
C<BASTION>, etc.) are addresses that have been registered with
AddressInfo(n). The rule FTP-7 has a port range that cannot be implemented
with IPFilter.

  IPFilter(// Spoof-1:
           deny src INTERNALNET,
           // HTTP-2:
           allow src BASTION && dst INTERNALNET
              && tcp && src port www && dst port > 1023 && ack,
           // Telnet-2:
           allow dst INTERNALNET
              && tcp && src port 23 && dst port > 1023 && ack,
           // SSH-2:
           allow dst INTERNALNET && tcp && src port 22 && ack,
           // SSH-3:
           allow dst INTERNALNET && tcp && dst port 22,
           // FTP-2:
           allow dst INTERNALNET
              && tcp && src port 21 && dst port > 1023 && ack,
           // FTP-4:
           allow dst INTERNALNET
              && tcp && src port > 1023 && dst port > 1023 && ack,
           // FTP-6:
           allow src BASTION && dst INTERNALNET
              && tcp && src port 21 && dst port > 1023 && ack,
           // FTP-7 omitted
           // FTP-8:
           allow src BASTION && dst INTERNALNET
              && tcp && src port > 1023 && dst port > 1023,
           // SMTP-2:
           allow src BASTION && dst INTERNAL_SMTP
              && tcp && src port 25 && dst port > 1023 && ack,
           // SMTP-3:
           allow src BASTION && dst INTERNAL_SMTP
              && tcp && src port > 1023 && dst port 25,
           // NNTP-2:
           allow src NNTP_FEED && dst INTERNAL_NNTP
              && tcp && src port 119 && dst port > 1023 && ack,
           // NNTP-3:
           allow src NNTP_FEED && dst INTERNAL_NNTP
              && tcp && src port > 1023 && dst port 119,
           // DNS-2:
           allow src BASTION && dst INTERNAL_DNS
              && udp && src port 53 && dst port 53,
           // DNS-4:
           allow src BASTION && dst INTERNAL_DNS
              && tcp && src port 53 && dst port > 1023 && ack,
           // DNS-5:
           allow src BASTION && dst INTERNAL_DNS
              && tcp && src port > 1023 && dst port 53,
           // Default-2:
           deny all);

=h program read-only
Returns a human-readable definition of the program the IPFilter element
is using to classify packets. At each step in the program, four bytes
of packet data are ANDed with a mask and compared against four bytes of
classifier pattern.

=h cache_hits_count read-only
If CACHING is enabled, the IPFilter element stores the last rule in a cache.
This handler returns the number of cache hits (i.e., number of input packets
that matched directly the cached rule, thereby did not have to traverse the
classification tree).
If CACHING is disabled, this handler returns -1.

=h cache_misses_count read-only
If CACHING is enabled, the IPFilter element stores the last rule in a cache.
This handler returns the number of cache misses (i.e., number of input packets
that did not match the cached rule, thereby had to traverse the
classification tree).
If CACHING is disabled, this handler returns -1.

=h cache_total_count read-only
If CACHING is enabled, the IPFilter element stores the last rule in a cache.
This handler returns the number of total accesses in the cache (i.e., the summary
of hits and misses).
If CACHING is disabled, this handler returns -1.

=h cache_hits_ratio read-only
If CACHING is enabled, the IPFilter element stores the last rule in a cache.
This handler returns the ratio of cache hits over the total number of accesses
in the cache.
This ratio ranges in [0, 100].
If CACHING is disabled, this handler returns -1.

=h cache_misses_ratio read-only
If CACHING is enabled, the IPFilter element stores the last rule in a cache.
This handler returns the ratio of cache misses over the total number of accesses
in the cache.
This ratio ranges in [0, 100].
If CACHING is disabled, this handler returns -1.

=a

IPClassifier, Classifier, CheckIPHeader, MarkIPHeader, CheckIPHeader2,
AddressInfo, tcpdump(1) */

class IPFilter : public BatchElement { public:

    IPFilter() CLICK_COLD;
    ~IPFilter() CLICK_COLD;

    static void static_initialize();
    static void static_cleanup();

    const char *class_name() const override      { return "IPFilter"; }
    const char *port_count() const override      { return "1/-"; }
    const char *processing() const override      { return PUSH; }
    // this element does not need AlignmentInfo; override Classifier's "A" flag
    const char *flags() const           { return ""; }
    bool can_live_reconfigure() const       { return true; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;

#if HAVE_BATCH
    void push_batch(int port, PacketBatch *);
#endif
    void push(int port, Packet *);

    typedef Classification::Wordwise::CompressedProgram IPFilterProgram;
    static void parse_program(IPFilterProgram &zprog,
                  const Vector<String> &conf, int noutputs,
                  const Element *context, ErrorHandler *errh);
    inline int match(const IPFilterProgram &zprog, const Packet *p);
    inline int match(Packet *p);

    enum {
        TYPE_NONE   = 0,        // data types
        TYPE_TYPE   = 1,
        TYPE_SYNTAX = 2,
        TYPE_INT    = 3,

        TYPE_HOST   = 10,       // expression types
        TYPE_PROTO  = 11,
        TYPE_IPFRAG = 12,
        TYPE_PORT   = 13,
        TYPE_TCPOPT = 14,
        TYPE_ETHER  = 15,

        TYPE_NET    = 30,       // shorthands
        TYPE_IPUNFRAG   = 31,
        TYPE_IPECT  = 32,
        TYPE_IPCE   = 33,

        TYPE_FIELD  = 0x40000000,
        // bit 31 must be zero
        // bit 30 must be one
        // bits 29-21 represent IP protocol (9 bits); 0 means no protocol
        // bits 20-5 represent field offset into header in bits (16 bits)
        // bits 4-0 represent field length in bits minus one (5 bits)
        FIELD_PROTO_SHIFT = 21,
        FIELD_PROTO_MASK = (0x1FF << FIELD_PROTO_SHIFT),
        FIELD_OFFSET_SHIFT = 5,
        FIELD_OFFSET_MASK = (0xFFFF << FIELD_OFFSET_SHIFT),
        FIELD_LENGTH_SHIFT = 0,
        FIELD_LENGTH_MASK = (0x1F << FIELD_LENGTH_SHIFT),
        FIELD_CSUM  = (TYPE_FIELD | ((10*8) << FIELD_OFFSET_SHIFT) | 15),
        FIELD_IPLEN = (TYPE_FIELD | ((2*8) << FIELD_OFFSET_SHIFT) | 15),
        FIELD_ID    = (TYPE_FIELD | ((4*8) << FIELD_OFFSET_SHIFT) | 15),
        FIELD_VERSION   = (TYPE_FIELD | (0 << FIELD_OFFSET_SHIFT) | 3),
        FIELD_HL    = (TYPE_FIELD | (4 << FIELD_OFFSET_SHIFT) | 3),
        FIELD_TOS   = (TYPE_FIELD | ((1*8) << FIELD_OFFSET_SHIFT) | 7),
        FIELD_DSCP  = (TYPE_FIELD | ((1*8) << FIELD_OFFSET_SHIFT) | 5),
        FIELD_TTL   = (TYPE_FIELD | ((8*8) << FIELD_OFFSET_SHIFT) | 7),
        FIELD_TCP_WIN = (TYPE_FIELD | (IP_PROTO_TCP << FIELD_PROTO_SHIFT) | ((14*8) << FIELD_OFFSET_SHIFT) | 15),
        FIELD_ICMP_TYPE = (TYPE_FIELD | (IP_PROTO_ICMP << FIELD_PROTO_SHIFT) | (0 << FIELD_OFFSET_SHIFT) | 7)
    };

    enum {
        UNKNOWN = -1000
    };

    enum {
        SD_SRC = 1, SD_DST = 2, SD_AND = 3, SD_OR = 4
    };

    enum {
        OP_EQ = 0, OP_GT = 1, OP_LT = 2
    };

    enum {
        // if you change this, change click-fastclassifier.cc also
        offset_mac = 0,
        offset_net = 256,
        offset_transp = 512
    };

    enum {
        PERFORM_BINARY_SEARCH = 1,
        MIN_BINARY_SEARCH = 7
    };

    union PrimitiveData {
        uint32_t u;
        int32_t i;
        struct in_addr ip4;
        unsigned char c[8];
    };

    struct Primitive {
        int _type;
        int _data;

        int _op;
        bool _op_negated;

        int _srcdst;
        int _transp_proto;

        PrimitiveData _u;
        PrimitiveData _mask;

        Primitive()         { clear(); }

        void clear();
        void set_type(int, ErrorHandler *);
        void set_srcdst(int, ErrorHandler *);
        void set_transp_proto(int, ErrorHandler *);

        int set_mask(uint32_t full_mask, int shift, uint32_t provided_mask,
                 ErrorHandler *errh);
        int check(const Primitive &prev_prim, int level,
              int mask_dt, const PrimitiveData &mask,
              ErrorHandler *errh);
        void compile(Classification::Wordwise::Program &p, Vector<int> &tree) const;

        bool has_transp_proto() const;
        bool negation_is_simple() const;
        void simple_negate();

        String unparse_type() const;
        String unparse_op() const;
        static String unparse_type(int srcdst, int type);
        static String unparse_transp_proto(int transp_proto);

          private:

        int type_error(ErrorHandler *errh, const char *msg) const;
        void add_comparison_exprs(Classification::Wordwise::Program &p, Vector<int> &tree, int offset, int shift, bool swapped, bool op_negate) const;
    };

  protected:

    // In caching mode, a set of per IPFilter element variables stores caching info
    struct IPFilterCache {
        IPFilterCache() : IPFilterCache(NULL, -1, 0, 0) {}

        IPFilterCache(
            IPFlow5ID *last_flow_id, int last_port,
            uint64_t cache_hits_nb, uint64_t cache_misses_nb
        ) : last_flow_id(last_flow_id), last_port(last_port),
            cache_hits_nb(cache_hits_nb), cache_misses_nb(cache_misses_nb) {}

        IPFlow5ID *last_flow_id;
        int last_port;
        uint64_t cache_hits_nb;
        uint64_t cache_misses_nb;
    };

    IPFilterProgram _zprog;
    bool _caching;
    IPFilterCache _cache;

    static String read_handler(Element *e, void *thunk);

    enum {
        H_PROGRAM,
        H_CACHE_HITS, H_CACHE_MISSES, H_CACHE_TOTAL,
        H_CACHE_HITS_RATIO, H_CACHE_MISSES_RATIO
    };

  private:

    static int lookup(String word, int type, int transp_proto, uint32_t &data,
              const Element *context, ErrorHandler *errh);

    static void add_pattern(Vector<String> &words, PrefixErrorHandler &cerrh, const Element *context, int noutputs, Vector<Classification::Wordwise::Program> &progs);
    struct Parser {
        const Vector<String> &_words;
        Vector<int> &_tree;
        Classification::Wordwise::Program &_prog;
        const Element *_context;
        ErrorHandler *_errh;
        Primitive _prev_prim;

        Parser(const Vector<String> &words, Vector<int> &tree,
               Classification::Wordwise::Program &prog,
               const Element *context, ErrorHandler *errh)
            : _words(words), _tree(tree), _prog(prog), _context(context),
              _errh(errh) {
        }

        struct parse_state {
            int state;
            int last_pos;
            parse_state(int s) : state(s) {}
        };
        enum {
            s_expr0, s_expr1, s_expr2,
            s_orexpr0, s_orexpr1,
            s_term0, s_term1, s_term2,
            s_factor0, s_factor1, s_factor2,
            s_factor0_neg, s_factor1_neg, s_factor2_neg
        };
        void parse_slot(int output, int pos);
        int parse_expr_iterative(int pos);
        int parse_test(int pos, bool negated);
    };

    static int length_checked_match(const IPFilterProgram &zprog, const Packet *p, int packet_length);

};


inline bool
IPFilter::Primitive::has_transp_proto() const
{
    return _transp_proto >= 0;
}

inline bool
IPFilter::Primitive::negation_is_simple() const
{
    if (_type == TYPE_PROTO)
        return true;
    else if (_transp_proto >= 0)
        return false;
    else
        return _type == TYPE_HOST || (_type & TYPE_FIELD) || _type == TYPE_IPFRAG;
}

inline int
IPFilter::match(const IPFilterProgram &zprog, const Packet *p)
{
    int packet_length = p->network_length(),
    network_header_length = p->network_header_length();
    if (packet_length > network_header_length)
        packet_length += offset_transp - network_header_length;
    else
        packet_length += offset_net;

    if (zprog.output_everything() >= 0) {
        if (_caching) {
            _cache.cache_misses_nb++;
        }
        return zprog.output_everything();
    }
    else if (packet_length < (int) zprog.safe_length()) {
        if (_caching) {
            _cache.cache_misses_nb++;
        }
        // common case never checks packet length
        return length_checked_match(zprog, p, packet_length);
    }

    // Caching enabled
    if (_caching) {
        if (_cache.last_flow_id) {
            // Get the flow ID of this packet
            IPFlow5ID new_flow_id(p);
            // Exploit last output port stored from a previous packet of the same flow
            if (new_flow_id == *_cache.last_flow_id) {
                int port = _cache.last_port;
                assert((port >= 0) && (port < noutputs()));
                _cache.cache_hits_nb++;
                return port;
            }
        }
        _cache.cache_misses_nb++;
    }

    const unsigned char *neth_data = p->network_header();
    const unsigned char *transph_data = p->transport_header();

    const uint32_t *pr = zprog.begin();
    const uint32_t *pp;
    uint32_t data;
    while (1) {
        int off = (int16_t) pr[0];
        if (off >= offset_transp)
            data = *(const uint32_t *)(transph_data + off - offset_transp);
        else if (off >= offset_net)
            data = *(const uint32_t *)(neth_data + off - offset_net);
        else
            data = *(const uint32_t *)(p->mac_header() - 2 + off);
        data &= pr[3];
        off = pr[0] >> 17;
        pp = pr + 4;
        if (!PERFORM_BINARY_SEARCH || off < MIN_BINARY_SEARCH) {
            for (; off; --off, ++pp)
                if (*pp == data) {
                    off = pr[2];
                    goto gotit;
                }
        } else {
            const uint32_t *px = pp + off;
            while (pp < px) {
                const uint32_t *pm = pp + (px - pp) / 2;
                if (*pm == data) {
                    off = pr[2];
                    goto gotit;
                } else if (*pm < data)
                    pp = pm + 1;
                else
                    px = pm;
            }
        }
        off = pr[1];
        gotit:
        if (off <= 0) {
            if (_caching) {
                IPFlow5ID new_flow_id(p);
                _cache.last_flow_id = &new_flow_id;
                _cache.last_port = -off;
            }
            return -off;
        }
        pr += off;
    }
}

inline int
IPFilter::match(Packet *p)
{
    return match(_zprog, p);
}

CLICK_ENDDECLS
#endif
