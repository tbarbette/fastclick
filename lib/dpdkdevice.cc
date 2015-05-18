/*
 * dpdkconfig.{cc,hh} -- library for interfacing with Intel's DPDK
 *
 * Copyright (c) 2014 Cyril Soldani, University of Liège
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

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_pci.h>



CLICK_DECLS

struct DevInfo {
    inline DevInfo() : n_rx_queues(0), n_tx_queues(0), promisc(false), n_rx_descs(256), n_tx_descs(1024) { }
    inline DevInfo(DpdkDevice::Dir dir, unsigned queue_no, bool promisc)
        : n_rx_queues((dir == DpdkDevice::RX) ? queue_no + 1 : 0)
        , n_tx_queues((dir == DpdkDevice::TX) ? queue_no + 1 : 0)
        , promisc(promisc)
        , n_rx_descs(256), n_tx_descs(1024) { }

    unsigned n_rx_queues;
    unsigned n_tx_queues;
    unsigned n_rx_descs;
    unsigned n_tx_descs;
    bool promisc;
};

static bool _is_initialized = false;

static HashMap<unsigned, DevInfo> _devs;

static struct rte_mempool **_pktmbuf_pools;

/* Wraps rte_eth_dev_socket_id(), which may return -1 for valid ports when NUMA
 * is not well supported. This function will return 0 instead in that case. */
int DpdkDevice::get_port_numa_node(unsigned port_no)
{
    if (port_no >= rte_eth_dev_count())
        return -1;
    int numa_node = rte_eth_dev_socket_id(port_no);
    return (numa_node == -1) ? 0 : numa_node;
}

unsigned int DpdkDevice::get_nb_txdesc(unsigned port_no)
{
    DevInfo *info = _devs.findp(port_no);
    if (!info) {
        return 0;
    }
    return info->n_tx_descs;
};

static bool alloc_pktmbufs()
{
    // Count sockets
    int max_socket = -1;
    for (HashMap<unsigned, DevInfo>::const_iterator it = _devs.begin();
         it != _devs.end(); ++it) {
        int numa_node = DpdkDevice::get_port_numa_node(it.key());
        if (numa_node > max_socket)
            max_socket = numa_node;
    }
    if (max_socket == -1)
        return false;

    // Allocate pktmbuf_pool array
    typedef struct rte_mempool *rte_mempool_p;
    _pktmbuf_pools = new rte_mempool_p[max_socket + 1];
    if (!_pktmbuf_pools)
        return false;
    memset(_pktmbuf_pools, 0,
           (max_socket + 1) * sizeof (struct rte_mempool *));

    // Create a pktmbuf pool for each active socket
    for (HashMap<unsigned, DevInfo>::const_iterator it = _devs.begin();
         it != _devs.end(); ++it) {
        int numa_node = DpdkDevice::get_port_numa_node(it.key());
        if (!_pktmbuf_pools[numa_node]) {
            char name[64];
            snprintf(name, 64, "mbuf_pool_%u", numa_node);
            _pktmbuf_pools[numa_node] =
                rte_mempool_create(
                    name, NB_MBUF, MBUF_SIZE, MBUF_CACHE_SIZE,
                    sizeof (struct rte_pktmbuf_pool_private),
                    rte_pktmbuf_pool_init, NULL, rte_pktmbuf_init, NULL,
                    numa_node, 0);
            if (!_pktmbuf_pools[numa_node])
                return false;
        }
    }

    return true;
}

struct rte_mempool * DpdkDevice::get_mpool(unsigned int socket_id) {
    return _pktmbuf_pools[socket_id];
}

static int initialize_device(unsigned port_no, const DevInfo &info,
                             ErrorHandler *errh)
{
    struct rte_eth_conf dev_conf;
    memset(&dev_conf, 0, sizeof dev_conf);
    dev_conf.rxmode.mq_mode = ETH_MQ_RX_RSS;
    dev_conf.rx_adv_conf.rss_conf.rss_key = NULL;
    dev_conf.rx_adv_conf.rss_conf.rss_hf = ETH_RSS_IP;

    if (rte_eth_dev_configure(port_no, info.n_rx_queues, info.n_tx_queues,
                              &dev_conf) < 0)
        return errh->error(
            "Cannot initialize DPDK port %u with %u RX and %u TX queues",
            port_no, info.n_rx_queues, info.n_tx_queues);
    struct rte_eth_rxconf rx_conf;
    memset(&rx_conf, 0, sizeof rx_conf);
    rx_conf.rx_thresh.pthresh = RX_PTHRESH;
    rx_conf.rx_thresh.hthresh = RX_HTHRESH;
    rx_conf.rx_thresh.wthresh = RX_WTHRESH;

    struct rte_eth_txconf tx_conf;
    memset(&tx_conf, 0, sizeof tx_conf);
    tx_conf.tx_thresh.pthresh = TX_PTHRESH;
    tx_conf.tx_thresh.hthresh = TX_HTHRESH;
    tx_conf.tx_thresh.wthresh = TX_WTHRESH;
    tx_conf.tx_free_thresh = 0; /* use default value */
    tx_conf.tx_rs_thresh = 0; /* use default value */
    tx_conf.txq_flags = ETH_TXQ_FLAGS_NOMULTSEGS | ETH_TXQ_FLAGS_NOOFFLOADS;

    int numa_node = DpdkDevice::get_port_numa_node(port_no);
    for (unsigned i = 0; i < info.n_rx_queues; ++i) {
        if (rte_eth_rx_queue_setup(
                port_no, i, info.n_rx_descs, numa_node, &rx_conf,
                _pktmbuf_pools[numa_node]) != 0)
            return errh->error(
                "Cannot initialize RX queue %u of port %u on node %u",
                i, port_no, numa_node);
    }

    for (unsigned i = 0; i < info.n_tx_queues; ++i)
        if (rte_eth_tx_queue_setup(port_no, i, info.n_tx_descs, numa_node, &tx_conf)
            != 0)
            return errh->error(
                "Cannot initialize TX queue %u of port %u on node %u",
                i, port_no, numa_node);

    int err = rte_eth_dev_start(port_no);
    if (err < 0)
        return errh->error(
            "Cannot start DPDK port %u: error %d", port_no, err);

    if (info.promisc)
        rte_eth_promiscuous_enable(port_no);

    return 0;
}

int DpdkDevice::add_device(unsigned port_no, DpdkDevice::Dir dir,
                           unsigned queue_no, bool promisc,
                           ErrorHandler *errh)
{
    if (_is_initialized)
        return errh->error(
            "Trying to configure DPDK device after initialization");

    DevInfo *info = _devs.findp(port_no);
    if (!info) {
        _devs.insert(port_no, DevInfo(dir, queue_no, promisc));
    } else {
        if (dir == RX && info->n_rx_queues > 0 && promisc != info->promisc)
            return errh->error(
                "Some elements disagree on whether or not device %u should be"
                " in promiscuous mode", port_no);
        info->promisc |= promisc;
        if (dir == RX && queue_no >= info->n_rx_queues)
            info->n_rx_queues = queue_no + 1;
        if (dir == TX && queue_no >= info->n_tx_queues)
            info->n_tx_queues = queue_no + 1;
    }

    return 0;
}

int DpdkDevice::add_rx_device(unsigned port_no, unsigned queue_no,
                              bool promisc, ErrorHandler *errh)
{
    return add_device(port_no, DpdkDevice::RX, queue_no, promisc, errh);
}

int DpdkDevice::add_tx_device(unsigned port_no, unsigned queue_no,
                              ErrorHandler *errh)
{
    return add_device(port_no, DpdkDevice::TX, queue_no, false, errh);
}

void DpdkDevice::set_rx_descs(unsigned port_no, unsigned rx_descs,
                              ErrorHandler *errh) {
    DevInfo *info = _devs.findp(port_no);
    if (!info) {
        errh->error("No port %u found",port_no);
        return;
    }
    info->n_rx_descs = rx_descs;
}

void DpdkDevice::set_tx_descs(unsigned port_no, unsigned tx_descs,
                              ErrorHandler *errh) {
    DevInfo *info = _devs.findp(port_no);
    if (!info) {
        errh->error("No port %u found",port_no);
        return;
    }
    info->n_tx_descs = tx_descs;
}

int DpdkDevice::initialize(ErrorHandler *errh)
{
    if (_is_initialized)
        return 0;

    click_chatter("Initializing DPDK");

    if (rte_eal_pci_probe())
        return errh->error("Cannot probe the PCI bus");

    // We should maybe do PCI probing and get some stats when DpdkConfig loads
    // so that we can check the following at configure time rather than during
    // initialization
    const unsigned n_ports = rte_eth_dev_count();
    if (n_ports == 0)
        return errh->error("No DPDK-enabled ethernet port found");
    for (HashMap<unsigned, DevInfo>::const_iterator it = _devs.begin();
         it != _devs.end(); ++it)
        if (it.key() >= n_ports)
            return errh->error("Cannot find DPDK port %u", it.key());

    if (!alloc_pktmbufs())
        return errh->error("Could not allocate packet MBuf pools");

    for (HashMap<unsigned, DevInfo>::const_iterator it = _devs.begin();
         it != _devs.end(); ++it) {
        int ret = initialize_device(it.key(), it.value(), errh);
        if (ret < 0)
            return ret;
    }

    _is_initialized = true;
    return 0;
}

#if !CLICK_DPDK_POOLS
void DpdkDevice::free_pkt(unsigned char *, size_t, void *pktmbuf)
{
	//click_chatter("Free %p",pktmbuf);
    rte_pktmbuf_free((struct rte_mbuf *) pktmbuf);
}

void DpdkDevice::fake_free_pkt(unsigned char *, size_t, void *)
{
	//click_chatter("Fake Free");
}
#endif

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk)
ELEMENT_PROVIDES(DpdkDevice)
