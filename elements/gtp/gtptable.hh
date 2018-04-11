#ifndef CLICK_GTPTable_HH
#define CLICK_GTPTable_HH
#include <click/batchelement.hh>
#include <click/ipflowid.hh>
#include <click/icmpflowid.hh>
#include <click/hashtablemp.hh>
CLICK_DECLS

class GTPFlowID {public:
    IPFlowID ip_id;
    uint32_t gtp_id;

    inline GTPFlowID() : ip_id(), gtp_id(0) {

    }

    inline GTPFlowID(IPFlowID ip, uint32_t gtp) : ip_id(ip), gtp_id(gtp){

    }

    /** @brief Hash function.
     * @return The hash value of this IPFlowID.
     *
     * Equal IPFlowID objects always have equal hashcode() values. */
    inline hashcode_t hashcode() const;

    inline bool operator==(const GTPFlowID &o) const{
        return o.ip_id == ip_id && o.gtp_id == gtp_id;
    }
};

inline hashcode_t GTPFlowID::hashcode() const
{
    // more complicated hashcode, but causes less collision
    hashcode_t hash = ip_id.hashcode();
    hash ^= gtp_id;
    return hash;
}

class GTPFlowIDMAP : public GTPFlowID{public:
    click_jiffies_t last_seen;
    bool known;

    inline GTPFlowIDMAP() : GTPFlowID(), last_seen(0),known(false) {

    }

    inline GTPFlowIDMAP(GTPFlowID id) : GTPFlowID(id), last_seen(0),known(false) {

    }
};

/*
=c

GTPTable()

=s gtp

decapsulate the GTP packet and set the GTP TEID in the aggregate annotation

=d

=a GTPEncap
*/

class GTPLookup;

class GTPTable : public BatchElement { public:

    GTPTable() CLICK_COLD;
    ~GTPTable() CLICK_COLD;

    const char *class_name() const	{ return "GTPTable"; }
    const char *port_count() const	{ return "2/2"; }
    const char *flow_code() const  { return "xy/xz"; }
    const char *flags() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const	{ return true; }

    int process(int, Packet*);
    void push(int, Packet *) override;
#if HAVE_BATCH
	void push_batch(int port, PacketBatch *) override;
#endif
  private:

	//Map from GTP_IN to GTP_OUT.
	typedef HashTableMP<GTPFlowID,GTPFlowIDMAP> GTPFlowTable;
	GTPFlowTable _gtpmap;

	//Map of Inner IP to GTP_IN, or GTP_OUT if known is set
	typedef HashTableMP<IPFlowID,GTPFlowIDMAP> INMap;
	INMap _inmap;

	typedef HashTableMP<ICMPFlowID,GTPFlowID> ResolvMap;
	ResolvMap _icmp_map;

	bool _verbose;
	IPAddress _ping_dst;

	friend class GTPLookup;

};

CLICK_ENDDECLS
#endif
