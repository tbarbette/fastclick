#ifndef CLICK_DPDKDEVICE_HH
#define CLICK_DPDKDEVICE_HH

//Prevent bug under some configurations (like travis-ci's one) where these macros get undefined
#ifndef UINT8_MAX
#define UINT8_MAX 255
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 65535
#endif

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_pci.h>
#include <rte_version.h>

#include <click/packet.hh>
#include <click/error.hh>
#include <click/hashtable.hh>
#include <click/vector.hh>
#include <click/args.hh>
#include <click/etheraddress.hh>

CLICK_DECLS
class DPDKDeviceArg;

#if HAVE_INT64_TYPES
typedef uint64_t counter_t;
#else
typedef uint32_t counter_t;
#endif

extern bool dpdk_enabled;

/**
 * DPDK's Flow Director API.
 */
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)

class DPDKDevice;

class FlowDirector {

public:
    FlowDirector();
    FlowDirector(uint8_t port_id, ErrorHandler *errh);
    ~FlowDirector();

    /**
     * Descriptor for a single flow.
     */
    struct port_flow {
        port_flow() :
            rule_id(0), flow(0), attr(0),
            pattern(0), actions(0) {};

        port_flow(
            uint32_t                id,
            struct rte_flow        *flow,
            struct rte_flow_attr   *attr,
            struct rte_flow_item   *pattern,
            struct rte_flow_action *actions
        ) : rule_id(id), flow(flow), attr(attr),
            pattern(pattern), actions(actions) {};

        uint32_t                rule_id;
        struct rte_flow        *flow;
        struct rte_flow_attr   *attr;
        struct rte_flow_item   *pattern;
        struct rte_flow_action *actions;
    };

    // Current list of flow rules
    Vector<struct port_flow *> _rule_list;

    // DPDKDevice mode
    static String FLOW_DIR_FLAG;

    // Supported flow director handlers (called from FromDPDKDevice)
    static String FLOW_RULE_ADD;
    static String FLOW_RULE_DEL;
    static String FLOW_RULE_LIST;
    static String FLOW_RULE_FLUSH;

    // Rule structure constants
    static String FLOW_RULE_PATTERN;
    static String FLOW_RULE_ACTION;
    static String FLOW_RULE_IP4_PREFIX;

    // Supported patterns
    static const Vector<String> FLOW_RULE_PATTERNS_VEC;

    // Supported actions
    static const Vector<String> FLOW_RULE_ACTIONS_VEC;

    // Global table of ports mapped to their Flow Director objects
    static HashTable<uint8_t, FlowDirector *> _dev_flow_dir;

    // Manages the Flow Director instances
    static FlowDirector *get_flow_director(
        const uint8_t &port_id,
        ErrorHandler *errh
    );

    inline static void delete_error_handler() { delete _errh; };

    // Port ID handlers
    inline void    set_port_id(const uint8_t &port_id) { _port_id = port_id; };
    inline uint8_t get_port_id() { return _port_id; };

    // Activation/deactivation handlers
    inline void set_active(const bool &active) { _active = active; };
    inline bool get_active() { return _active; };

    // Verbosity handlers
    inline void set_verbose(const bool &verbose) { _verbose = verbose; };
    inline bool get_verbose() { return _verbose; };

    // Rules' file handlers
    inline void   set_rules_filename(const String &file) { _rules_filename = file; };
    inline String get_rules_filename() { return _rules_filename; };

    // Add rules from a file
    static void add_rules_file(
        const uint8_t &port_id,
        const String &filename
    );

    // Count the rules
    static uint32_t flow_rules_count(const uint8_t &port_id);

    // Flush the rules
    static uint32_t flow_rules_flush(const uint8_t &port_id);

    // Parse a string-based rule and translate it into a flow rule object
    static bool flow_rule_install(
        const uint8_t  &port_id,
        const uint32_t &rule_id,
        const String   &rule
    );

    // Delete a flow rule
    static bool flow_rule_delete(
        const uint8_t  &port_id,
        const uint32_t &rule_id
    );

    // Return a flow rule object with a specific ID
    static struct port_flow *flow_rule_get(
        const uint8_t  &port_id,
        const uint32_t &rule_id
    );

private:

    // Device ID
    uint8_t _port_id;

    // Indicates whether Flow Director is active for the given device
    bool _active;

    // Set stdout verbosity
    bool _verbose;

    // Filename that contains the rules to be installed
    String _rules_filename;

    // A unique error handler
    static ErrorVeneer *_errh;

    // Clean up the rules of a particular NIC
    static uint32_t memory_clean(const uint8_t &port_id);

    // Reports whether a flow rule would be accepted by the device
    static bool flow_rule_validate(
        const uint8_t                &port_id,
        const uint32_t               &rule_id,
        const struct rte_flow_attr   *attr,
        const struct rte_flow_item   *patterns,
        const struct rte_flow_action *actions
    );

    // Adds a newly-created flow rule to the NIC and to our memory
    static bool flow_rule_add(
        const uint8_t                &port_id,
        const uint32_t               &rule_id,
        const struct rte_flow_attr   *attr,
        const struct rte_flow_item   *patterns,
        const struct rte_flow_action *actions
    );

    // Reports problems that occur during the NIC configuration
    static int flow_rule_complain(
        const uint8_t &port_id,
        struct rte_flow_error *error
    );

    // Reports the correct usage of a Flow Director rule along with a message
    static void flow_rule_usage(
        const uint8_t &port_id,
        const char *message
    );
};

#endif

class DPDKDevice {
public:

    uint8_t port_id;

    DPDKDevice() : port_id(-1) {
    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        initialize_flow_director(port_id, ErrorHandler::default_handler());
    #endif
    }

    DPDKDevice(uint8_t port_id) : port_id(port_id) {
    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        initialize_flow_director(port_id, ErrorHandler::default_handler());
    #endif
    } CLICK_COLD;

    struct DevInfo {
        inline DevInfo() :
            rx_queues(0,false), tx_queues(0,false), promisc(false), n_rx_descs(0),
            n_tx_descs(0), mq_mode((enum rte_eth_rx_mq_mode)-1), mq_mode_str(""),
            num_pools(0), vf_vlan(), mac() {
            rx_queues.reserve(128);
            tx_queues.reserve(128);
        }

        String get_mq_mode() { return mq_mode_str; }

        Vector<bool> rx_queues;
        Vector<bool> tx_queues;
        bool promisc;
        unsigned n_rx_descs;
        unsigned n_tx_descs;
        enum rte_eth_rx_mq_mode mq_mode;
        String mq_mode_str;
        int num_pools;
        Vector<int> vf_vlan;
        EtherAddress mac;
    };

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    void initialize_flow_director(const uint8_t &port_id, ErrorHandler *errh);
#endif

    int add_rx_queue(
        unsigned &queue_id, bool promisc,
        unsigned n_desc, ErrorHandler *errh
    ) CLICK_COLD;

    int add_tx_queue(
        unsigned &queue_id, unsigned n_desc,
        ErrorHandler *errh
    ) CLICK_COLD;

    void set_mac(EtherAddress mac);

    unsigned int get_nb_txdesc();

    static struct rte_mempool *get_mpool(unsigned int);

    static int get_port_numa_node(uint8_t port_id);

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    int set_mode(
        String mode, int num_pools, Vector<int> vf_vlan,
        const String &rules_path, ErrorHandler *errh
    );
#else
    int set_mode(
        String mode, int num_pools, Vector<int> vf_vlan,
        ErrorHandler *errh
    );
#endif

    static int initialize(ErrorHandler *errh);

    int static_cleanup();

    inline static bool is_dpdk_packet(Packet* p) {
        return p->buffer_destructor() == DPDKDevice::free_pkt;
    }

    inline static bool is_dpdk_buffer(Packet* p) {
        return is_dpdk_packet(p) || (p->data_packet() && is_dpdk_packet(p->data_packet()));
    }

    inline static rte_mbuf* get_pkt(unsigned numa_node);
    inline static rte_mbuf* get_pkt();
    inline static struct rte_mbuf* get_mbuf(Packet* p, bool create, int node);

    static void free_pkt(unsigned char *, size_t, void *pktmbuf);

    static unsigned int get_nb_txdesc(uint8_t port_id);

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    static void configure_nic(const uint8_t &port_id);
#endif

    static void cleanup(ErrorHandler *errh);

    static int NB_MBUF;
    static int MBUF_DATA_SIZE;
    static int MBUF_SIZE;
    static int MBUF_CACHE_SIZE;
    static int RX_PTHRESH;
    static int RX_HTHRESH;
    static int RX_WTHRESH;
    static int TX_PTHRESH;
    static int TX_HTHRESH;
    static int TX_WTHRESH;
    static String MEMPOOL_PREFIX;

    static unsigned DEF_RING_NDESC;
    static unsigned DEF_BURST_SIZE;

    static unsigned RING_FLAGS;
    static unsigned RING_SIZE;
    static unsigned RING_POOL_CACHE_SIZE;
    static unsigned RING_PRIV_DATA_SIZE;

    static struct rte_mempool** _pktmbuf_pools;

    inline struct DevInfo getInfo() { return info; };
    inline int nbRXQueues();
    inline int nbTXQueues();
    inline int nbVFPools();

    struct ether_addr gen_mac(int a, int b);

private:

    enum Dir { RX, TX };

    struct DevInfo info;

    static bool _is_initialized;
    static HashTable<uint8_t, DPDKDevice> _devs;
    static unsigned _nr_pktmbuf_pools;
    static bool no_more_buffer_msg_printed;

    int initialize_device(ErrorHandler *errh) CLICK_COLD;
    int add_queue(Dir dir, unsigned &queue_id, bool promisc,
                   unsigned n_desc, ErrorHandler *errh) CLICK_COLD;

    static int alloc_pktmbufs() CLICK_COLD;

    static DPDKDevice* get_device(const uint8_t &port_id) {
        return &(_devs.find_insert(port_id, DPDKDevice(port_id)).value());
    }

    static int get_port_from_pci(uint16_t domain, uint8_t bus, uint8_t dev_id, uint8_t function) {
       struct rte_eth_dev_info dev_info;
       for (uint8_t port_id = 0 ; port_id < rte_eth_dev_count(); ++port_id) {
          rte_eth_dev_info_get(port_id, &dev_info);
          struct rte_pci_addr addr = dev_info.pci_dev->addr;
          if (addr.domain   == domain &&
              addr.bus      == bus &&
              addr.devid    == dev_id &&
              addr.function == function)
              return port_id;
       }
       return -1;
    }

    friend class DPDKDeviceArg;
};

/** @class DPDKDeviceArg
  @brief Parser class for DPDK Port, either an integer or a PCI address. */
class DPDKDeviceArg { public:
    static bool parse(const String &str, DPDKDevice* &result, const ArgContext &args = ArgContext());
    static String unparse(DPDKDevice* dev) {
        return String(dev->port_id);
    }
};

template<> struct DefaultArg<DPDKDevice*> : public DPDKDeviceArg {};

/**
 * Get a DPDK mbuf from a packet. If the packet buffer is a DPDK buffer, it will
 *     give that one. If it isn't, it will allocate a new mbuf from a DPDK pool
 *     and copy its content.
 *     If compiled with CLICK_PACKET_USE_DPDK, it will simply return the packet
 *     casted as it's already a DPDK buffer.
 */
inline struct rte_mbuf* DPDKDevice::get_mbuf(Packet* p, bool create, int node) {
    struct rte_mbuf* mbuf;
    #if CLICK_PACKET_USE_DPDK
    mbuf = p->mb();
    #else
    if (likely(DPDKDevice::is_dpdk_packet(p) && (mbuf = (struct rte_mbuf *) p->destructor_argument()))
        || unlikely(p->data_packet() && DPDKDevice::is_dpdk_packet(p->data_packet()) && (mbuf = (struct rte_mbuf *) p->data_packet()->destructor_argument()))) {
        /* If the packet is an unshared DPDK packet, we can send
         *  the mbuf as it to DPDK*/
        rte_pktmbuf_pkt_len(mbuf) = p->length();
        rte_pktmbuf_data_len(mbuf) = p->length();
        mbuf->data_off = p->headroom();
        if (p->shared()) {
            /*Prevent DPDK from freeing the buffer. When all shared packet
             * are freed, DPDKDevice::free_pkt will effectively destroy it.*/
            rte_mbuf_refcnt_update(mbuf, 1);
        } else {
            //Reset buffer, let DPDK free the buffer when it wants
            p->reset_buffer();
        }
    } else {
        if (create) {
            /*The packet is not a DPDK packet, or it is shared : we need to allocate a mbuf and
             * copy the packet content to it.*/
            mbuf = DPDKDevice::get_pkt(node);
            if (mbuf == 0) {
                return NULL;
            }
            memcpy((void*)rte_pktmbuf_mtod(mbuf, unsigned char *),p->data(),p->length());
            rte_pktmbuf_pkt_len(mbuf) = p->length();
            rte_pktmbuf_data_len(mbuf) = p->length();
        } else
            return NULL;
    }
    #endif
    return mbuf;
}

inline rte_mbuf* DPDKDevice::get_pkt(unsigned numa_node) {
    struct rte_mbuf* mbuf = rte_pktmbuf_alloc(get_mpool(numa_node));
    if (unlikely(!mbuf)) {
        if (!DPDKDevice::no_more_buffer_msg_printed)
            click_chatter("No more DPDK buffer available ! Try using "
                               "DPDKInfo to allocate more.");
        else
            DPDKDevice::no_more_buffer_msg_printed = true;
    }
    return mbuf;
}

inline rte_mbuf* DPDKDevice::get_pkt() {
    return get_pkt(rte_socket_id());
}

int DPDKDevice::nbRXQueues() {
    return info.rx_queues.size();
};

int DPDKDevice::nbTXQueues() {
    return info.tx_queues.size();
};

int DPDKDevice::nbVFPools() {
    return info.num_pools;
};

CLICK_ENDDECLS

#endif
