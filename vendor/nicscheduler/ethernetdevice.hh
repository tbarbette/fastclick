#ifndef LIBNICSCHEDULER_ETHDEVICE_HH
#define LIBNICSCHEDULER_ETHDEVICE_HH
#include <vector>

//DPDK includes, if enabled
#if HAVE_DPDK
/**
 * Unified type for DPDK port IDs.
 * Until DPDK v17.05 was uint8_t
 * After DPDK v17.05 has been uint16_t
 */
#include <rte_version.h>
#ifndef PORTID_T_DEFINED
    #define PORTID_T_DEFINED
# if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    typedef uint16_t portid_t;
# else
    typedef uint8_t portid_t;
# endif
#else
    // Already defined in <testpmd.h>
#endif
#endif


struct EthernetDevice;

typedef std::vector<unsigned> (*eth_get_rss_reta)(struct EthernetDevice* eth);
typedef int (*eth_get_rss_reta_size)(struct EthernetDevice* eth);
typedef int (*eth_set_rss_reta)(struct EthernetDevice* eth, unsigned* table, unsigned table_sz);

struct EthernetDevice {
	EthernetDevice() : get_rss_reta_size(0), set_rss_reta(0), get_rss_reta(0)  {

	}

	eth_get_rss_reta_size get_rss_reta_size;
	eth_set_rss_reta set_rss_reta;
	eth_get_rss_reta get_rss_reta;
};

#if HAVE_DPDK
struct DPDKEthernetDevice : public EthernetDevice {
    portid_t port_id;

    portid_t get_port_id() { return port_id; }
};
#endif

#endif
