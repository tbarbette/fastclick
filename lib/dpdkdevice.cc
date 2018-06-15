/*
 * dpdkdevice.{cc,hh} -- library for interfacing with DPDK
 * Cyril Soldani, Tom Barbette
 *
 * Integration of DPDK's Flow API by Georgios Katsikas
 *
 * Copyright (c) 2014-2016 University of Liege
 * Copyright (c) 2016 Cisco Meraki
 * Copyright (c) 2017 RISE SICS
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/element.hh>
#include <click/dpdkdevice.hh>
#include <click/userutils.hh>
#include <rte_errno.h>


#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    #include <click/flowdirector.hh>
extern "C" {
    #include <rte_pmd_ixgbe.h>
}
#endif

CLICK_DECLS

DPDKDevice::DPDKDevice() : port_id(-1), info() {
}

DPDKDevice::DPDKDevice(portid_t port_id) : port_id(port_id) {
    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        if (port_id >= 0)
            initialize_flow_director(port_id, ErrorHandler::default_handler());
    #endif
};

uint16_t DPDKDevice::get_device_vendor_id()
{
    return info.vendor_id;
}

String DPDKDevice::get_device_vendor_name()
{
    return info.vendor_name;
}

uint16_t DPDKDevice::get_device_id()
{
    return info.device_id;
}

const char *DPDKDevice::get_device_driver()
{
    return info.driver;
}

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
/**
 * Called by the constructor of DPDKDevice.
 * Flow Director must be strictly invoked once for each port.
 *
 * @param port_id the ID of the device where Flow Director is invoked
 */
void DPDKDevice::initialize_flow_director(
        const portid_t &port_id, ErrorHandler *errh)
{
    FlowDirector *flow_dir = FlowDirector::get_flow_director(port_id, errh);
    if (!flow_dir) {
        return;
    }

    // Verify
    const portid_t p_id = flow_dir->get_port_id();
    assert((p_id >= 0) && (p_id == port_id));
}
#endif /* RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0) */


/* Wraps rte_eth_dev_socket_id(), which may return -1 for valid ports when NUMA
 * is not well supported. This function will return 0 instead in that case. */
int DPDKDevice::get_port_numa_node(portid_t port_id)
{
    if (port_id >= dev_count())
        return -1;
    int numa_node = rte_eth_dev_socket_id(port_id);
    return (numa_node == -1) ? 0 : numa_node;
}

unsigned int DPDKDevice::get_nb_txdesc()
{
    return info.n_tx_descs;
}

/**
 * This function is called by DPDK when Click runs as a secondary process.
 * It checks that the prefix matches with the given config prefix and adds
 * it if it does so.
 */
#if RTE_VERSION >= RTE_VERSION_NUM(16,07,0,0)
void add_pool(struct rte_mempool *rte, void *arg){
#else
void add_pool(const struct rte_mempool *rte, void *arg){
#endif
    int *i = (int *) arg;
    if (strncmp(
            DPDKDevice::MEMPOOL_PREFIX.c_str(),
            const_cast<struct rte_mempool *>(rte)->name,
            DPDKDevice::MEMPOOL_PREFIX.length()) != 0)
        return;
    DPDKDevice::_pktmbuf_pools[*i] = const_cast<struct rte_mempool *>(rte);
    click_chatter("Found DPDK primary pool #%d %s",*i, DPDKDevice::_pktmbuf_pools[*i]->name);
    (*i)++;
}

int core_to_numa_node(unsigned lcore_id) {
       int numa_node = rte_lcore_to_socket_id(lcore_id);
       return (numa_node < 0) ? 0 : numa_node;
}

int DPDKDevice::alloc_pktmbufs()
{
    /* Count NUMA sockets for each device and each node, we do not want to
     * allocate a unused pool
     */
    int max_socket = -1;
    for (HashTable<portid_t, DPDKDevice>::const_iterator it = _devs.begin();
         it != _devs.end(); ++it) {
        int numa_node = DPDKDevice::get_port_numa_node(it.key());
        if (numa_node > max_socket)
            max_socket = numa_node;
    }
    int i;
    RTE_LCORE_FOREACH(i) {
        int numa_node = core_to_numa_node(i);
        if (numa_node > max_socket)
            max_socket = numa_node;
    }


    if (max_socket == -1)
        max_socket = 0;

    int n_pktmbuf_pools = max_socket + 1;

    // Allocate pktmbuf_pool array
    typedef struct rte_mempool *rte_mempool_p;
    if (_nr_pktmbuf_pools < n_pktmbuf_pools) {
        auto pktmbuf_pools = new rte_mempool_p[n_pktmbuf_pools];
        if (!pktmbuf_pools)
            return false;
        for (int i = 0; i < _nr_pktmbuf_pools; i++) {
            pktmbuf_pools[i] = _pktmbuf_pools[i];
        }
        if (_pktmbuf_pools)
            delete[] _pktmbuf_pools;
        for (int i = _nr_pktmbuf_pools; i < n_pktmbuf_pools; i++) {
            pktmbuf_pools[i] = 0;
        }
        _pktmbuf_pools = pktmbuf_pools;
        _nr_pktmbuf_pools = n_pktmbuf_pools;
    }

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        // Create a pktmbuf pool for each active socket
        for (int i = 0; i < _nr_pktmbuf_pools; i++) {
                if (!_pktmbuf_pools[i]) {
                        String mempool_name = DPDKDevice::MEMPOOL_PREFIX + String(i);
                        const char* name = mempool_name.c_str();
                        _pktmbuf_pools[i] =
#if RTE_VERSION >= RTE_VERSION_NUM(2,2,0,0)
                        rte_pktmbuf_pool_create(name, NB_MBUF,
                                                MBUF_CACHE_SIZE, 0, MBUF_DATA_SIZE, i);
#else
                        rte_mempool_create(
                                        name, NB_MBUF, MBUF_SIZE, MBUF_CACHE_SIZE,
                                        sizeof (struct rte_pktmbuf_pool_private),
                                        rte_pktmbuf_pool_init, NULL, rte_pktmbuf_init, NULL,
                                        i, 0);
#endif

                        if (!_pktmbuf_pools[i])
                                return rte_errno;
                }
        }
    } else {
        int i = 0;
        rte_mempool_walk(add_pool,(void*)&i);
        if (i == 0) {
            click_chatter("Could not get pools from the primary DPDK process");
            return -1;
        }
    }

    return 0;
}

struct rte_mempool *DPDKDevice::get_mpool(unsigned int socket_id) {
    return _pktmbuf_pools[socket_id];
}

/**
 * Extracts from 'info' what is after the 'key'.
 * E.g. an expected input is:
 * XX:YY.Z Ethernet controller: Mellanox Technologies MT27700 Family [ConnectX-4]
 * and we want to keep what is after our key 'Ethernet controller: '.
 *
 * @param info string to parse
 * @param key substring to indicate the new index
 * @return substring of info that succeeds the key
 */
static String parse_pci_info(String info, String key)
{
    String s;

    // Extract what is after the keyword
    s = info.substring(info.find_left(key) + key.length());
    if (s.empty()) {
        return String();
    }

    // Find the position of the delimiter
    int pos = s.find_left(':') + 2;
    if (pos < 0) {
        return String();
    }

    // Extract what comes after the delimiter
    s = s.substring(pos, s.find_left("\n") - pos);
    if (s.empty()) {
        return String();
    }

    return s;
}

/**
 * Keeps the left-most substring of 'str'
 * until the first occurence of the delimiter.
 *
 * @param str string to parse
 * @param delimiter character that indicates where to stop
 * @return substring of str that preceds the delimiter
 */
static String keep_token_left(String str, char delimiter)
{
    return str.substring(0, str.find_left(delimiter));
}


#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
int DPDKDevice::set_mode(
        String mode, int num_pools, Vector<int> vf_vlan,
        const String &rules_path, ErrorHandler *errh) {
#else
int DPDKDevice::set_mode(
        String mode, int num_pools, Vector<int> vf_vlan,
        ErrorHandler *errh) {
#endif
    mode = mode.lower();

    enum rte_eth_rx_mq_mode m;

    if (mode == "") {
        return 0;
    } else if (mode == "none") {
        m = ETH_MQ_RX_NONE;
    } else if ((mode == "rss") || (mode == FlowDirector::FLOW_DIR_MODE)) {
        m = ETH_MQ_RX_RSS;
    } else if (mode == "vmdq") {
        m = ETH_MQ_RX_VMDQ_ONLY;
    } else if (mode == "vmdq_rss") {
        m = ETH_MQ_RX_VMDQ_RSS;
    } else if (mode == "vmdq_dcb") {
        m = ETH_MQ_RX_VMDQ_DCB;
    } else if (mode == "vmdq_dcb_rss") {
        m = ETH_MQ_RX_VMDQ_DCB_RSS;
    } else {
        return errh->error("Unknown mode %s",mode.c_str());
    }

    if (m != info.mq_mode && info.mq_mode != -1) {
        return errh->error("Device can only have one mode.");
    }

    if (m & ETH_MQ_RX_VMDQ_FLAG) {
        if (num_pools != info.num_pools && info.num_pools != 0) {
            return errh->error(
                "Number of VF pools must be consistent for the same device"
            );
        }
        if (vf_vlan.size() > 0) {
            if (info.vf_vlan.size() > 0)
                return errh->error(
                    "VF_VLAN can only be setted once per device"
                );
            if (vf_vlan.size() != num_pools) {
                return errh->error(
                    "Number of VF_VLAN must be equal to the number of pools"
                );
            }
            info.vf_vlan = vf_vlan;
        }

        if (num_pools) {
            info.num_pools = num_pools;
        }

    }

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    if (mode == FlowDirector::FLOW_DIR_MODE) {
        FlowDirector *flow_dir = FlowDirector::get_flow_director(port_id, errh);
        flow_dir->set_active(true);
        flow_dir->set_rules_filename(rules_path);
        errh->message(
            "Flow Director (port %u): State active - Source file '%s'",
            port_id, rules_path.empty() ? "None" : rules_path.c_str()
        );
    }
#endif

    info.mq_mode = m;
    info.mq_mode_str = mode;

    return 0;
}

rte_eth_rx_mq_mode DPDKDevice::get_mode() {
    return getInfo().mq_mode;
}

String DPDKDevice::get_mode_str() {
    return getInfo().mq_mode_str;
}

static struct ether_addr pool_addr_template = {
        .addr_bytes = {0x52, 0x54, 0x00, 0x00, 0x00, 0x00}
};

struct ether_addr DPDKDevice::gen_mac( int a, int b) {
    struct ether_addr mac;
     if (info.init_mac != EtherAddress()) {
         memcpy(&mac,info.init_mac.data(),sizeof(struct ether_addr));
     } else
         mac = pool_addr_template;
    mac.addr_bytes[4] = a;
    mac.addr_bytes[5] = b;
    return mac;
}

int DPDKDevice::initialize_device(ErrorHandler *errh)
{
    struct rte_eth_conf dev_conf;
    struct rte_eth_dev_info dev_info;
    memset(&dev_conf, 0, sizeof dev_conf);

    rte_eth_dev_info_get(port_id, &dev_info);

#if RTE_VERSION >= RTE_VERSION_NUM(17,11,0,0) && RTE_VERSION < RTE_VERSION_NUM(18,05,0,0)
    if (strcmp(dev_info.driver_name,"net_mlx5") == 0) {
        errh->warning("WARNING : DPDK 17.11 to 18.02 included have broken support for secondary process with mlx5. Use 18.05 with mlx5 cards if you use secondary process.");
    }
#endif

    info.mq_mode = (info.mq_mode == -1? ETH_MQ_RX_RSS : info.mq_mode);
    dev_conf.rxmode.mq_mode = info.mq_mode;
    dev_conf.rxmode.hw_vlan_filter = 0;

    if (info.mq_mode & ETH_MQ_RX_VMDQ_FLAG) {

        if (info.num_pools > dev_info.max_vmdq_pools) {
            return errh->error(
                "The number of VF Pools exceeds the hardware limit of %d",
                dev_info.max_vmdq_pools
            );
        }

        if (info.rx_queues.size() % info.num_pools != 0) {
            info.rx_queues.resize(
                ((info.rx_queues.size() / info.num_pools) + 1) * info.num_pools
            );
        }
        dev_conf.rx_adv_conf.vmdq_rx_conf.nb_queue_pools = 
            (enum rte_eth_nb_pools) info.num_pools;
        dev_conf.rx_adv_conf.vmdq_rx_conf.enable_default_pool = 0;
        dev_conf.rx_adv_conf.vmdq_rx_conf.default_pool = 0;
        if (info.vf_vlan.size() > 0) {
            dev_conf.rx_adv_conf.vmdq_rx_conf.rx_mode = 0;
            dev_conf.rx_adv_conf.vmdq_rx_conf.nb_pool_maps = info.num_pools;
            for (int i = 0; i < dev_conf.rx_adv_conf.vmdq_rx_conf.nb_pool_maps; i++) {
                dev_conf.rx_adv_conf.vmdq_rx_conf.pool_map[i].vlan_id =
                    info.vf_vlan[i];
                dev_conf.rx_adv_conf.vmdq_rx_conf.pool_map[i].pools =
                    (1UL << (i % info.num_pools));
            }
        } else {
            dev_conf.rx_adv_conf.vmdq_rx_conf.rx_mode = ETH_VMDQ_ACCEPT_UNTAG;
            dev_conf.rx_adv_conf.vmdq_rx_conf.nb_pool_maps = 0;
        }

    }
    if (info.mq_mode & ETH_MQ_RX_RSS_FLAG) {
        dev_conf.rx_adv_conf.rss_conf.rss_key = NULL;
        dev_conf.rx_adv_conf.rss_conf.rss_hf =
            ETH_RSS_IP | ETH_RSS_UDP | ETH_RSS_TCP;
    }

#if RTE_VERSION < RTE_VERSION_NUM(18,05,0,0)
    // Obtain general device information
    if (dev_info.pci_dev) {
        info.vendor_id = dev_info.pci_dev->id.vendor_id;
        info.device_id = dev_info.pci_dev->id.device_id;
    }
#else
    //TODO
#endif
    info.driver = dev_info.driver_name;
    info.vendor_name = "Unknown";

    // Combine vendor and device IDs
    char vendor_and_dev[10];
    sprintf(vendor_and_dev, "%x:%x", info.vendor_id, info.device_id);

    // Retrieve more information about the vendor of this NIC
    String dev_pci = shell_command_output_string("lspci -d " + String(vendor_and_dev), "", errh);
    String long_vendor_name = parse_pci_info(dev_pci, "Ethernet controller");
    if (!long_vendor_name.empty()) {
        info.vendor_name = keep_token_left(long_vendor_name, ' ');
    }

    //We must open at least one queue per direction
    if (info.rx_queues.size() == 0) {
        info.rx_queues.resize(1);
        info.n_rx_descs = DEF_DEV_RXDESC;
    }
    if (info.tx_queues.size() == 0) {
        info.tx_queues.resize(1);
        info.n_tx_descs = DEF_DEV_TXDESC;
    }

    if (info.rx_queues.size() > dev_info.max_rx_queues) {
        return errh->error("Port %d can only use %d RX queues (asked for %d), use MAXQUEUES to set the maximum "
                           "number of queues or N_QUEUES to strictly define it.", port_id, dev_info.max_rx_queues, info.rx_queues.size());
    }
    if (info.tx_queues.size() > dev_info.max_tx_queues) {
        return errh->error("Port %d can only use %d TX queues (FastClick asked for %d, probably to serve that same amount of threads).\n"
                           "Add the argument \"MAXQUEUES %d\" to the corresponding ToDPDKDevice to set the maximum "
                           "number of queues to %d or \"N_QUEUES %d\" to strictly define it. "
                           "If the TX device has more threads than queues due to this parameter change, it will automatically rely on locking to share the queues as evenly as possible between the threads.", port_id, dev_info.max_tx_queues, info.tx_queues.size(), dev_info.max_tx_queues, dev_info.max_tx_queues, dev_info.max_tx_queues);
    }

    if (info.n_rx_descs < dev_info.rx_desc_lim.nb_min || info.n_rx_descs > dev_info.rx_desc_lim.nb_max) {
        return errh->error("The number of receive descriptors is %d but needs to be between %d and %d",info.n_rx_descs, dev_info.rx_desc_lim.nb_min, dev_info.rx_desc_lim.nb_max);
    }

    if (info.n_tx_descs < dev_info.tx_desc_lim.nb_min || info.n_tx_descs > dev_info.tx_desc_lim.nb_max) {
        return errh->error("The number of transmit descriptors is %d but needs to be between %d and %d",info.n_tx_descs, dev_info.tx_desc_lim.nb_min, dev_info.tx_desc_lim.nb_max);
    }

    int ret;
    if (ret = rte_eth_dev_configure(
            port_id, info.rx_queues.size(),
            info.tx_queues.size(), &dev_conf) < 0)
        return errh->error(
            "Cannot initialize DPDK port %u with %u RX and %u TX queues\nError %d : %s",
            port_id, info.rx_queues.size(), info.tx_queues.size(),
            ret, strerror(ret));

    rte_eth_dev_info_get(port_id, &dev_info);

#if RTE_VERSION >= RTE_VERSION_NUM(16,07,0,0)
    if (dev_info.nb_rx_queues != info.rx_queues.size()) {
        return errh->error("Device only initialized %d RX queues instead of %d. "
                "Please check configuration.", dev_info.nb_rx_queues,
                info.rx_queues.size());
    }
    if (dev_info.nb_tx_queues != info.tx_queues.size()) {
        return errh->error("Device only initialized %d TX queues instead of %d. "
                "Please check configuration.", dev_info.nb_tx_queues,
                info.tx_queues.size());
    }
#endif

    struct rte_eth_rxconf rx_conf;
#if RTE_VERSION >= RTE_VERSION_NUM(2,0,0,0)
    memcpy(&rx_conf, &dev_info.default_rxconf, sizeof rx_conf);
#else
    bzero(&rx_conf,sizeof rx_conf);
#endif
    rx_conf.rx_thresh.pthresh = RX_PTHRESH;
    rx_conf.rx_thresh.hthresh = RX_HTHRESH;
    rx_conf.rx_thresh.wthresh = RX_WTHRESH;

    struct rte_eth_txconf tx_conf;
#if RTE_VERSION >= RTE_VERSION_NUM(2,0,0,0)
    memcpy(&tx_conf, &dev_info.default_txconf, sizeof tx_conf);
#else
    bzero(&tx_conf,sizeof tx_conf);
#endif
    tx_conf.tx_thresh.pthresh = TX_PTHRESH;
    tx_conf.tx_thresh.hthresh = TX_HTHRESH;
    tx_conf.tx_thresh.wthresh = TX_WTHRESH;
    tx_conf.txq_flags |= ETH_TXQ_FLAGS_NOMULTSEGS | ETH_TXQ_FLAGS_NOOFFLOADS;

    int numa_node = DPDKDevice::get_port_numa_node(port_id);
    for (unsigned i = 0; i < info.rx_queues.size(); ++i) {
        if (rte_eth_rx_queue_setup(
                port_id, i, info.n_rx_descs, numa_node, &rx_conf,
                _pktmbuf_pools[numa_node]) != 0)
            return errh->error(
                "Cannot initialize RX queue %u of port %u on node %u : %s",
                i, port_id, numa_node, rte_strerror(rte_errno));
    }

    for (unsigned i = 0; i < info.tx_queues.size(); ++i)
        if (rte_eth_tx_queue_setup(port_id, i, info.n_tx_descs, numa_node,
                                   &tx_conf) != 0)
            return errh->error(
                "Cannot initialize TX queue %u of port %u on node %u",
                i, port_id, numa_node);

    if (info.init_mtu != 0) {
        if (rte_eth_dev_set_mtu(port_id, info.init_mtu) != 0) {
            return errh->error("Could not set MTU %d",info.init_mtu);
        }
    }

    int err = rte_eth_dev_start(port_id);
    if (err < 0)
        return errh->error(
            "Cannot start DPDK port %u: error %d", port_id, err);

    if (info.promisc)
        rte_eth_promiscuous_enable(port_id);

    if (info.init_mac != EtherAddress()) {
        struct ether_addr addr;
        memcpy(&addr,info.init_mac.data(),sizeof(struct ether_addr));
        if (rte_eth_dev_default_mac_addr_set(port_id, &addr) != 0) {
            return errh->error("Could not set default MAC address");
        }
    }

    if (info.mq_mode & ETH_MQ_RX_VMDQ_FLAG) {
        /*
         * Set mac for each pool and parameters
         */
        for (unsigned q = 0; q < info.num_pools; q++) {
                struct ether_addr mac;
                mac = gen_mac(port_id, q);
                printf("Port %u vmdq pool %u set mac %02x:%02x:%02x:%02x:%02x:%02x\n",
                        port_id, q,
                        mac.addr_bytes[0], mac.addr_bytes[1],
                        mac.addr_bytes[2], mac.addr_bytes[3],
                        mac.addr_bytes[4], mac.addr_bytes[5]);
                int retval = rte_eth_dev_mac_addr_add(port_id, &mac,
                                q);
                if (retval) {
                        printf("mac addr add failed at pool %d\n", q);
                        return retval;
                }
        }
    }

    return 0;
}

void DPDKDevice::set_init_mac(EtherAddress mac) {
    assert(!_is_initialized);
    info.init_mac = mac;
}

void DPDKDevice::set_init_mtu(uint16_t mtu) {
    assert(!_is_initialized);
    info.init_mtu = mtu;
}

EtherAddress DPDKDevice::get_mac() {
    assert(_is_initialized);
    struct ether_addr addr;
    rte_eth_macaddr_get(port_id,&addr);
    return EtherAddress((unsigned char*)&addr);
}

/**
 * Set v[id] to true in vector v, expanding it if necessary. If id is 0,
 * the first available slot will be taken.
 * If v[id] is already true, this function return false. True if it is a
 *   new slot or if the existing slot was false.
 */
bool set_slot(Vector<bool> &v, unsigned &id) {
    if (id <= 0) {
        int i;
        for (i = 0; i < v.size(); i ++) {
            if (!v[i]) break;
        }
        id = i;
        if (id >= v.size())
            v.resize(id + 1, false);
    }
    if (id >= v.size()) {
        v.resize(id + 1,false);
    }
    if (v[id])
        return false;
    v[id] = true;
    return true;
}

int DPDKDevice::add_queue(DPDKDevice::Dir dir,
                           unsigned &queue_id, bool promisc, unsigned n_desc,
                           ErrorHandler *errh)
{
    if (_is_initialized) {
        return errh->error(
            "Trying to configure DPDK device after initialization");
    }

    if (dir == RX) {
        if (info.rx_queues.size() > 0 && promisc != info.promisc)
            return errh->error(
                "Some elements disagree on whether or not device %u should"
                " be in promiscuous mode", port_id);
        info.promisc |= promisc;
        if (n_desc > 0) {
            if (n_desc != info.n_rx_descs && info.rx_queues.size() > 0)
                return errh->error(
                        "Some elements disagree on the number of RX descriptors "
                        "for device %u", port_id);
            info.n_rx_descs = n_desc;
        }
        if (!set_slot(info.rx_queues, queue_id))
            return errh->error(
                        "Some elements are assigned to the same RX queue "
                        "for device %u", port_id);
    } else {
        if (n_desc > 0) {
            if (n_desc != info.n_tx_descs && info.tx_queues.size() > 0)
                return errh->error(
                        "Some elements disagree on the number of TX descriptors "
                        "for device %u", port_id);
            info.n_tx_descs = n_desc;
        }
        if (!set_slot(info.tx_queues,queue_id))
            return errh->error(
                        "Some elements are assigned to the same TX queue "
                        "for device %u", port_id);
    }

    return 0;
}

int DPDKDevice::add_rx_queue(unsigned &queue_id, bool promisc,
                              unsigned n_desc, ErrorHandler *errh)
{
    return add_queue(DPDKDevice::RX, queue_id, promisc, n_desc, errh);
}

int DPDKDevice::add_tx_queue(unsigned &queue_id, unsigned n_desc,
                              ErrorHandler *errh)
{
    return add_queue(DPDKDevice::TX, queue_id, false, n_desc, errh);
}

int DPDKDevice::static_initialize(ErrorHandler* errh) {
#if HAVE_DPDK_PACKET_POOL
    if (!dpdk_enabled) {
        return errh->error("You must start Click with --dpdk option when compiling with --enable-dpdk-pool");
    }
#endif
    if (alloc_pktmbufs()) {
        errh->error("Could not allocate packet MBuf pools : error %d (%s)",rte_errno,rte_strerror(rte_errno));
        if (rte_errno == 12) {
            errh->error("Maybe try to allocate less buffers with DPDKInfo(X) or allocate more memory to DPDK by giving/increasing the -m parameter or allocate more hugepages.");
        }
        return -1;
    }
    return 0;
}

int DPDKDevice::initialize(ErrorHandler *errh)
{
    int err = 0;

    if (_is_initialized)
        return 0;

    pool_addr_template.addr_bytes[2] = click_random();
    pool_addr_template.addr_bytes[3] = click_random();

    if (!dpdk_enabled)
        return errh->error( "Supply the --dpdk argument to use DPDK.");

    click_chatter("Initializing DPDK");
#if RTE_VERSION < RTE_VERSION_NUM(2,0,0,0)
    if (rte_eal_pci_probe())
        return errh->error("Cannot probe the PCI bus");
#endif

    if (dev_count() == 0 && _devs.size() > 0)
        return errh->error("No DPDK-enabled ethernet port found");

    for (HashTable<portid_t, DPDKDevice>::const_iterator it = _devs.begin();
         it != _devs.end(); ++it)
        if (it.key() >= dev_count())
            return errh->error("Cannot find DPDK port %u", it.key());

    err = static_initialize(errh);
    if (err != 0)
        return err;

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        for (HashTable<portid_t, DPDKDevice>::iterator it = _devs.begin();
            it != _devs.end(); ++it) {
            int ret = it.value().initialize_device(errh);
            if (ret < 0)
                return ret;
        }
    }

    _is_initialized = true;

    // Configure Flow Director
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    for (HashTable<portid_t, FlowDirector *>::iterator
            it = FlowDirector::_dev_flow_dir.begin();
            it != FlowDirector::_dev_flow_dir.end(); ++it) {
        const portid_t port_id = it.key();

        // Only if the device is registered and has the correct mode
        if (_devs[port_id].info.mq_mode_str == FlowDirector::FLOW_DIR_MODE) {
            int err = DPDKDevice::configure_nic(port_id);
            if (err != 0) {
                errh->error("Error %d while configuring FLowDirector", err);
                return -1;
            }
        }
    }
#endif

    return 0;
}

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
int DPDKDevice::configure_nic(const portid_t &port_id)
{
    if (_is_initialized) {
        // Invoke Flow Director only if active
        if (FlowDirector::_dev_flow_dir[port_id]->get_active()) {
            // Retrieve the file that contains the rules (if any)
            String rules_file = FlowDirector::_dev_flow_dir[port_id]->get_rules_filename();

            // There is a file with rules (user-defined)
            if (!rules_file.empty()) {
                return FlowDirector::add_rules_from_file(port_id, rules_file);
            }
        }
    }
    return 0;
}
#endif

void DPDKDevice::free_pkt(unsigned char *, size_t, void *pktmbuf)
{
    rte_pktmbuf_free((struct rte_mbuf *) pktmbuf);
}

void DPDKDevice::cleanup(ErrorHandler *errh)
{
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    for (HashTable<portid_t, FlowDirector *>::const_iterator
            it = FlowDirector::_dev_flow_dir.begin();
            it != FlowDirector::_dev_flow_dir.end(); ++it) {
        if (it == NULL) {
            continue;
        }

        portid_t port_id = it.key();

        // Flush
        uint32_t rules_flushed = FlowDirector::flow_rules_flush(port_id);

        // Delete this instance
        delete it.value();

        // Report
        if (rules_flushed > 0) {
            errh->message(
                "Flow Director (port %u): Flushed %d rules from the NIC",
                port_id, rules_flushed
            );
        }
    }

    // Clean up the table
    FlowDirector::_dev_flow_dir.clear();
#endif
}

bool
DPDKDeviceArg::parse(
    const String &str, DPDKDevice* &result, const ArgContext &ctx)
{
    portid_t port_id;

    if (!IntArg().parse(str, port_id)) {
#if RTE_VERSION >= RTE_VERSION_NUM(18,05,0,0)
       uint16_t id;
       if (rte_eth_dev_get_port_by_name(str.c_str(), &id) != 0)
           return false;
       else
           port_id = id;
#else
       //Try parsing a ffff:ff:ff.f format. Code adapted from EtherAddressArg::parse
        unsigned data[4];
        int d = 0, p = 0;
        const char *s, *end = str.end();

        for (s = str.begin(); s != end; ++s) {
           int digit;
           if (*s >= '0' && *s <= '9')
             digit = *s - '0';
           else if (*s >= 'a' && *s <= 'f')
             digit = *s - 'a' + 10;
           else if (*s >= 'A' && *s <= 'F')
             digit = *s - 'A' + 10;
           else {
             if (((*s == ':' && d < 2) ||
                (*s == '.' && d == 2)) &&
                (p == 1 || (d < 3 && p == 2) || (d == 0 && (p == 3 || p == 4)))
                && d < 3) {
               p = 0;
               ++d;
               continue;
             } else
               break;
           }

           if ((d == 0 && p == 4) || (d > 0 && p == 2)||
                (d == 3 && p == 1) || d == 4)
               break;

           data[d] = (p ? data[d] << 4 : 0) + digit;
           ++p;
        }

        if (s == end && p != 0 && d != 3) {
            ctx.error("invalid id or invalid PCI address format");
            return false;
        }

        port_id = DPDKDevice::get_port_from_pci(
            data[0], data[1], data[2], data[3]
        );
#endif
    }

    if (port_id >= 0 && port_id < DPDKDevice::dev_count()) {
        result = DPDKDevice::get_device(port_id);
    }
    else {
        ctx.error("Cannot resolve PCI address to DPDK device");
        return false;
    }

    return true;
}

DPDKRing::DPDKRing() :
    _message_pool(0),
       _numa_zone(0), _burst_size(0), _flags(0), _ring(0), _force_create(false), _force_lookup(false),
       _count(0), _MEM_POOL("") {
}

DPDKRing::~DPDKRing() {

}

int
DPDKRing::parse(Args* args) {
    bool spenq = false;
    bool spdeq = false;
    String origin;
    String destination;
    _flags = 0;
    const Element* e = args->context();
    ErrorHandler* errh = args->errh();

    if (args ->  read_p("MEM_POOL",  _MEM_POOL)
            .read_p("FROM_PROC", origin)
            .read_p("TO_PROC",   destination)
            .read("BURST",        _burst_size)
            .read("NDESC",        _ndesc)
            .read("NUMA_ZONE",    _numa_zone)
            .read("SP_ENQ", spenq)
            .read("SC_DEQ", spdeq)
            .read("FORCE_LOOKUP", _force_lookup)
            .read("FORCE_CREATE", _force_create)
            .execute() < 0)
        return -1;

    if (spenq)
        _flags |= RING_F_SP_ENQ;
    if (spdeq)
        _flags |= RING_F_SC_DEQ;

    if ( _MEM_POOL.empty() || (_MEM_POOL.length() == 0) ) {
        _MEM_POOL = "0";
    }

    if (origin.empty() || destination.empty() ) {
        errh->error("Enter FROM_PROC and TO_PROC names");
        return -1;
    }

    if ( _ndesc == 0 ) {
        _ndesc = DPDKDevice::DEF_RING_NDESC;
        click_chatter("Default number of descriptors is set (%d)\n",
                        e->name().c_str(), _ndesc);
    }

    _MEM_POOL = DPDKDevice::MEMPOOL_PREFIX + _MEM_POOL;

    // If user does not specify the port number
    // we assume that the process belongs to the
    // memory zone of device 0.
    // TODO: Search the Click DAG to find a FromDPDKDevice, take its' port_id
    //       and use _numa_zone = DPDKDevice::get_port_numa_node(_port_id);
    if ( _numa_zone < 0 ) {
        click_chatter("[%s] Assuming NUMA zone 0\n", e->name().c_str());
        _numa_zone = 0;
    }

    _PROC_1 = origin+"_2_"+destination;
    _PROC_2 = destination+"_2_"+origin;

    return 0;
}

#if HAVE_DPDK_PACKET_POOL
/**
 * Must be able to fill the packet data pool,
 * and then have some packets for I/O.
 */
int DPDKDevice::NB_MBUF = 32*4096*2;
#else
int DPDKDevice::NB_MBUF = 65536;
#endif
#ifdef RTE_MBUF_DEFAULT_BUF_SIZE
int DPDKDevice::MBUF_DATA_SIZE = RTE_MBUF_DEFAULT_BUF_SIZE;
#else
int DPDKDevice::MBUF_DATA_SIZE = 2048 + RTE_PKTMBUF_HEADROOM;
#endif
int DPDKDevice::MBUF_SIZE = MBUF_DATA_SIZE 
                          + sizeof (struct rte_mbuf);
int DPDKDevice::MBUF_CACHE_SIZE = 256;
int DPDKDevice::RX_PTHRESH = 8;
int DPDKDevice::RX_HTHRESH = 8;
int DPDKDevice::RX_WTHRESH = 4;
int DPDKDevice::TX_PTHRESH = 36;
int DPDKDevice::TX_HTHRESH = 0;
int DPDKDevice::TX_WTHRESH = 0;
String DPDKDevice::MEMPOOL_PREFIX = "click_mempool_";

unsigned DPDKDevice::DEF_DEV_RXDESC = 256;
unsigned DPDKDevice::DEF_DEV_TXDESC = 256;

unsigned DPDKDevice::DEF_RING_NDESC = 1024;
unsigned DPDKDevice::DEF_BURST_SIZE = 32;

unsigned DPDKDevice::RING_SIZE  = 64;
unsigned DPDKDevice::RING_POOL_CACHE_SIZE = 32;
unsigned DPDKDevice::RING_PRIV_DATA_SIZE  = 0;

bool DPDKDevice::_is_initialized = false;
HashTable<portid_t, DPDKDevice> DPDKDevice::_devs;
struct rte_mempool** DPDKDevice::_pktmbuf_pools;
unsigned DPDKDevice::_nr_pktmbuf_pools;
bool DPDKDevice::no_more_buffer_msg_printed = false;

CLICK_ENDDECLS
