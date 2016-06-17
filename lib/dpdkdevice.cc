/*
 * dpdkdevice.{cc,hh} -- library for interfacing with Intel's DPDK
 *
 * Copyright (c) 2014-2015 Cyril Soldani, University of Liège
 * Copyright (c) 2015 Tom Barbette, University of Liège
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
#include <click/dpdkdevice.hh>

#include <rte_errno.h>

CLICK_DECLS

/* Wraps rte_eth_dev_socket_id(), which may return -1 for valid ports when NUMA
 * is not well supported. This function will return 0 instead in that case. */
int DPDKDevice::get_port_numa_node(unsigned port_id)
{
    if (port_id >= rte_eth_dev_count())
        return -1;
    int numa_node = rte_eth_dev_socket_id(port_id);
    return (numa_node == -1) ? 0 : numa_node;
}

unsigned int DPDKDevice::get_nb_txdesc(unsigned port_id)
{
    DevInfo *info = _devs.findp(port_id);
    if (!info)
        return 0;

    return info->n_tx_descs;
}

/**
 * This function is called by DPDK when Click run as a secondary process. It
 * 	checks that the prefix match with the given config prefix and add it if it does so.
 */
void DPDKDevice::add_pool(const struct rte_mempool * rte, void *arg){
	int* i = (int*)arg;
	if (strncmp(DPDKDevice::MEMPOOL_PREFIX.c_str(), const_cast<struct rte_mempool *>(rte)->name, DPDKDevice::MEMPOOL_PREFIX.length()) != 0)
		return;
	_pktmbuf_pools[*i] = const_cast<struct rte_mempool *>(rte);
	click_chatter("Found DPDK pool %s",*i,rte,_pktmbuf_pools[*i]->name);
	(*i)++;
}

int core_to_numa_node(unsigned lcore_id) {
       int numa_node = rte_lcore_to_socket_id(lcore_id);
       return (numa_node < 0) ? 0 : numa_node;
}

bool DPDKDevice::alloc_pktmbufs()
{
    /* Count NUMA sockets for each device and each node, we do not want to
     * allocate a unused pool
     */
    int max_socket = -1;
    for (HashMap<unsigned, DevInfo>::const_iterator it = _devs.begin();
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
        return false;

    _nr_pktmbuf_pools = max_socket + 1;

    // Allocate pktmbuf_pool array
    typedef struct rte_mempool *rte_mempool_p;
    _pktmbuf_pools = new rte_mempool_p[_nr_pktmbuf_pools];
    if (!_pktmbuf_pools)
        return false;
    memset(_pktmbuf_pools, 0, _nr_pktmbuf_pools * sizeof(rte_mempool_p));

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
		// Create a pktmbuf pool for each active socket
		for (int i = 0; i < _nr_pktmbuf_pools; i++) {
			if (!_pktmbuf_pools[i]) {
				const char* name = (DPDKDevice::MEMPOOL_PREFIX + String(i)).c_str();
				_pktmbuf_pools[i] =
#if defined(RTE_VER_YEAR) || (RTE_VER_MAJOR >= 2 && RTE_VER_MINOR >= 1)
					rte_pktmbuf_pool_create(name, NB_MBUF,
							MBUF_CACHE_SIZE, 0, MBUF_DATA_SIZE, i);
#else
					rte_mempool_create(
						name, NB_MBUF, MBUF_DATA_SIZE + sizeof (struct rte_mbuf), MBUF_CACHE_SIZE,
						sizeof (struct rte_pktmbuf_pool_private),
						rte_pktmbuf_pool_init, NULL, rte_pktmbuf_init, NULL,
						i, 0);
#endif

				if (!_pktmbuf_pools[i])
					return false;
			}
		}
    } else {
		int i = 0;
		rte_mempool_walk(add_pool,(void*)&i);
		if (i == 0) {
			click_chatter("Could not get pools from the primary DPDK process");
			return false;
		}
    }

    return true;
}

struct rte_mempool *DPDKDevice::get_mpool(unsigned int socket_id) {
    return _pktmbuf_pools[socket_id];
}

int DPDKDevice::initialize_device(unsigned port_id, DevInfo &info,
                                  ErrorHandler *errh)
{
    struct rte_eth_conf dev_conf;
    struct rte_eth_dev_info dev_info;
    memset(&dev_conf, 0, sizeof dev_conf);

    rte_eth_dev_info_get(port_id, &dev_info);

    dev_conf.rxmode.mq_mode = ETH_MQ_RX_RSS;
    dev_conf.rx_adv_conf.rss_conf.rss_key = NULL;
    dev_conf.rx_adv_conf.rss_conf.rss_hf = ETH_RSS_IP;

    //We must open at least one queue per direction
    if (info.rx_queues.size() == 0)
        info.rx_queues.resize(1);
    if (info.tx_queues.size() == 0)
        info.tx_queues.resize(1);

    if (rte_eth_dev_configure(port_id, info.rx_queues.size(), info.tx_queues.size(),
                              &dev_conf) < 0)
        return errh->error(
            "Cannot initialize DPDK port %u with %u RX and %u TX queues",
            port_id, info.rx_queues.size(), info.tx_queues.size());
    struct rte_eth_rxconf rx_conf;
#if defined(RTE_VER_YEAR) || RTE_VER_MAJOR >= 2
    memcpy(&rx_conf, &dev_info.default_rxconf, sizeof rx_conf);
#else
    bzero(&rx_conf,sizeof rx_conf);
#endif
    rx_conf.rx_thresh.pthresh = RX_PTHRESH;
    rx_conf.rx_thresh.hthresh = RX_HTHRESH;
    rx_conf.rx_thresh.wthresh = RX_WTHRESH;

    struct rte_eth_txconf tx_conf;
#if defined(RTE_VER_YEAR) || RTE_VER_MAJOR >= 2
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
                port_id, i, info.n_rx_descs > 0 ? info.n_rx_descs : 256 , numa_node, &rx_conf,
                _pktmbuf_pools[numa_node]) != 0)
            return errh->error(
                "Cannot setup RX queue %u of port %u on node %u",
                i, port_id, numa_node);
    }

    for (unsigned i = 0; i < info.tx_queues.size(); ++i)
        if (rte_eth_tx_queue_setup(port_id, i, info.n_tx_descs > 0 ? info.n_tx_descs : 1024, numa_node,
                                   &tx_conf) != 0)
            return errh->error(
                "Cannot setup TX queue %u of port %u on node %u",
                i, port_id, numa_node);

    int err = rte_eth_dev_start(port_id);
    if (err < 0)
        return errh->error(
            "Cannot start DPDK port %u: error %d", port_id, err);

    if (info.promisc)
        rte_eth_promiscuous_enable(port_id);

    return 0;
}

/**
 * Set v[id] to true in vector v, expanding it if necessary. If id is 0, the first
 * 	available slot will be taken.
 * If v[id] is already true, this function return false. True if it is a new slot
 * 	or if the existing slot was false.
 */
bool set_slot(Vector<bool> &v, int &id) {
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

int DPDKDevice::add_device(unsigned port_id, DPDKDevice::Dir dir,
                           int &queue_id, bool promisc, unsigned n_desc,
                           ErrorHandler *errh)
{
    if (_is_initialized)
        return errh->error(
            "Trying to configure DPDK device after initialization");

    DevInfo *info = _devs.findp(port_id);
    if (!info) {
        _devs.insert(port_id, DevInfo());
        info = _devs.findp(port_id);
    }

	if (dir == RX) {
		if (info->rx_queues.size() > 0 && promisc != info->promisc)
			return errh->error(
				"Some elements disagree on whether or not device %u should"
				" be in promiscuous mode", port_id);
		info->promisc |= promisc;
		if (n_desc > 0) {
			if (info->n_rx_descs != 0 && n_desc != info->n_rx_descs)
				return errh->error(
						"Some elements disagree on the number of RX descriptors "
						"for device %u", port_id);
			info->n_rx_descs = n_desc;
		}
		if (!set_slot(info->rx_queues,queue_id))
			return errh->error(
						"Some elements are assigned to the same RX queue "
						"for device %u", port_id);
	} else {
		if (n_desc > 0) {
			if (info->n_tx_descs != 0 && n_desc != info->n_tx_descs)
				return errh->error(
						"Some elements disagree on the number of TX descriptors "
						"for device %u", port_id);
			info->n_tx_descs = n_desc;
		}
		if (!set_slot(info->tx_queues,queue_id))
			return errh->error(
						"Some elements are assigned to the same TX queue "
						"for device %u", port_id);
	}

    return 0;
}

int DPDKDevice::add_rx_device(unsigned port_id, int &queue_id, bool promisc,
                              unsigned n_desc, ErrorHandler *errh)
{
    return add_device(
        port_id, DPDKDevice::RX, queue_id, promisc, n_desc, errh);
}

int DPDKDevice::add_tx_device(unsigned port_id, int &queue_id, unsigned n_desc,
                              ErrorHandler *errh)
{
    return add_device(port_id, DPDKDevice::TX, queue_id, false, n_desc, errh);
}

int DPDKDevice::static_cleanup() {
	if (!_is_initialized)
		return 0;
}

int DPDKDevice::initialize(ErrorHandler *errh)
{
    if (_is_initialized)
        return 0;

    click_chatter("Initializing DPDK");
#if !defined(RTE_VER_YEAR) && (RTE_VER_MAJOR < 2)
    if (rte_eal_pci_probe())
        return errh->error("Cannot probe the PCI bus");
#endif

    const unsigned n_ports = rte_eth_dev_count();
    if (n_ports == 0)
        return errh->error("No DPDK-enabled ethernet port found");

    for (HashMap<unsigned, DevInfo>::const_iterator it = _devs.begin();
         it != _devs.end(); ++it)
        if (it.key() >= n_ports)
            return errh->error("Cannot find DPDK port %u", it.key());

    if (!alloc_pktmbufs())
        return errh->error("Could not allocate packet MBuf pools. Errno %d : %s", rte_errno, rte_strerror(rte_errno));

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        for (HashMap<unsigned, DevInfo>::iterator it = _devs.begin();
         it != _devs.end(); ++it) {
            int ret = initialize_device(it.key(), it.value(), errh);
            if (ret < 0)
                return ret;
        }
    }

    _is_initialized = true;
    return 0;
}

void DPDKDevice::free_pkt(unsigned char * h, size_t, void *pktmbuf)
{
	struct rte_mbuf* mb = (struct rte_mbuf *) pktmbuf;
	rte_pktmbuf_free(mb);
}

int DPDKDevice::NB_MBUF = 65536;
int DPDKDevice::MBUF_DATA_SIZE =
    2048 + RTE_PKTMBUF_HEADROOM;
int DPDKDevice::MBUF_CACHE_SIZE = 256;
int DPDKDevice::RX_PTHRESH = 8;
int DPDKDevice::RX_HTHRESH = 8;
int DPDKDevice::RX_WTHRESH = 4;
int DPDKDevice::TX_PTHRESH = 36;
int DPDKDevice::TX_HTHRESH = 0;
int DPDKDevice::TX_WTHRESH = 0;
String DPDKDevice::MEMPOOL_PREFIX = "click_mempool_";

unsigned DPDKDevice::DEF_RING_NDESC = 1024;
unsigned DPDKDevice::DEF_BURST_SIZE = 32;

unsigned DPDKDevice::RING_FLAGS = 0;
unsigned DPDKDevice::RING_SIZE  = 64;
unsigned DPDKDevice::RING_POOL_CACHE_SIZE = 32;
unsigned DPDKDevice::RING_PRIV_DATA_SIZE  = 0;

bool DPDKDevice::_is_initialized = false;
HashMap<unsigned, DPDKDevice::DevInfo> DPDKDevice::_devs;
struct rte_mempool** DPDKDevice::_pktmbuf_pools;
int DPDKDevice::_nr_pktmbuf_pools;

CLICK_ENDDECLS
