// -*- related-file-name: "../../lib/ICMPFlowID.cc" -*-
#ifndef CLICK_ICMPFlowID_HH
#define CLICK_ICMPFlowID_HH
#include <click/ipaddress.hh>
#include <click/hashcode.hh>
#include <clicknet/ip.h>
#include <clicknet/icmp.h>
CLICK_DECLS
class Packet;

class ICMPFlowID { public:

    typedef uninitialized_type uninitialized_t;


    /** @brief Construct an empty flow ID.
     *
     * The empty flow ID has zero-valued addresses and ports. */
    ICMPFlowID()
    : _saddr(), _daddr(), _id(0), _seq(0) {
    }

    /** @brief Construct a flow ID with the given parts.
     * @param saddr source address
     * @param seq source port, in network order
     * @param daddr destination address
     * @param id destination port, in network order */
    ICMPFlowID(IPAddress saddr, IPAddress daddr, uint16_t id, uint16_t seq)
    : _saddr(saddr), _daddr(daddr), _id(id), _seq(seq) {
    }

    inline explicit ICMPFlowID(const Packet *p, bool reverse = false);
    inline explicit ICMPFlowID(const click_ip *iph, bool reverse = false);

    /** @brief Construct an uninitialized flow ID. */
    inline ICMPFlowID(const uninitialized_type &unused) {
    (void) unused;
    }


    typedef IPAddress (ICMPFlowID::*unspecified_bool_type)() const;
    /** @brief Return true iff the addresses of this flow ID are zero. */
    operator unspecified_bool_type() const {
    return _saddr || _daddr ? &ICMPFlowID::saddr : 0;
    }


    /** @brief Return this flow's source address. */
    IPAddress saddr() const {
    return _saddr;
    }

    /** @brief Return this flow's destination address. */
    IPAddress daddr() const {
    return _daddr;
    }

    /** @brief Return this flow's source address. */
    uint16_t seq() const {
    return _seq;
    }

    /** @brief Return this flow's source address. */
    uint16_t id() const {
    return _id;
    }

    /** @brief Set this flow's source address to @a a. */
    void set_saddr(IPAddress a) {
    _saddr = a;
    }

    /** @brief Set this flow's destination address to @a a. */
    void set_daddr(IPAddress a) {
    _daddr = a;
    }


    void assign(IPAddress saddr, IPAddress daddr, uint16_t id, uint16_t seq) {
    _saddr = saddr;
    _daddr = daddr;
    _id = id;
    _seq = seq;
    }


    ICMPFlowID reverse() const {
    return ICMPFlowID(_daddr,_saddr, _id, _seq);
    }
    inline ICMPFlowID rev() const CLICK_DEPRECATED;

    /** @brief Hash function.
     * @return The hash value of this ICMPFlowID.
     *
     * Equal ICMPFlowID objects always have equal hashcode() values. */
    inline hashcode_t hashcode() const;


  protected:

    // note: several functions depend on this field order!
    IPAddress _saddr;
    IPAddress _daddr;
    uint16_t _id;            // network byte order
    uint16_t _seq;            // network byte order

    int unparse(char *s) const;
    friend StringAccum &operator<<(StringAccum &sa, const ICMPFlowID &flow_id);

};


inline ICMPFlowID ICMPFlowID::rev() const
{
    return reverse();
}


#define ROT(v, r) ((v)<<(r) | ((unsigned)(v))>>(32-(r)))

inline hashcode_t ICMPFlowID::hashcode() const
{
    // more complicated hashcode, but causes less collision
    uint16_t s = ntohs(id());
    uint16_t d = ntohs(seq());
    hashcode_t sx = CLICK_NAME(hashcode)(saddr());
    hashcode_t dx = CLICK_NAME(hashcode)(daddr());
    return (ROT(sx, (s % 16) + 1) ^ ROT(dx, 31 - (d % 16)))
    ^ ((d << 16) | s);
}

#undef ROT

inline bool operator==(const ICMPFlowID &a, const ICMPFlowID &b)
{
    return a.seq() == b.seq() && a.id() == b.id()
    && a.saddr() == b.saddr() && a.daddr() == b.daddr();
}

inline bool operator!=(const ICMPFlowID &a, const ICMPFlowID &b)
{
    return a.seq() != b.seq() || a.id() != b.id()
    || a.saddr() != b.saddr() || a.daddr() != b.daddr();
}



inline
ICMPFlowID::ICMPFlowID(const Packet *p, bool reverse)
{
    const click_ip *iph = p->ip_header();
    const click_icmp_sequenced *icmph = reinterpret_cast<const click_icmp_sequenced*>(p->transport_header());
    assert(p->has_network_header() && p->has_transport_header()
       && IP_FIRSTFRAG(iph));

    if (likely(!reverse))
    assign(iph->ip_src.s_addr,
           iph->ip_dst.s_addr, icmph->icmp_identifier, icmph->icmp_sequence);
    else
    assign(iph->ip_dst.s_addr,
           iph->ip_src.s_addr, icmph->icmp_identifier, icmph->icmp_sequence);
}

inline
ICMPFlowID::ICMPFlowID(const click_ip *iph, bool reverse)
{
    const click_icmp_sequenced *icmph = reinterpret_cast<const click_icmp_sequenced*>(iph + 1);
    assert(IP_FIRSTFRAG(iph));

    if (likely(!reverse))
    assign(iph->ip_src.s_addr,
           iph->ip_dst.s_addr, icmph->icmp_identifier, icmph->icmp_sequence);
    else
    assign(iph->ip_dst.s_addr,
           iph->ip_src.s_addr, icmph->icmp_identifier, icmph->icmp_sequence);
}

CLICK_ENDDECLS
#endif
