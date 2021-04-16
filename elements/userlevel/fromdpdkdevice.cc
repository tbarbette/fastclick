// -*- c-basic-offset: 4; related-file-name: "fromdpdkdevice.hh" -*-
/*
 * fromdpdkdevice.{cc,hh} -- element reads packets live from network via
 * the DPDK. Configures DPDK-based NICs via DPDK's Flow API.
 *
 * Copyright (c) 2014-2015 Cyril Soldani, University of Liège
 * Copyright (c) 2016-2017 Tom Barbette, University of Liège
 * Copyright (c) 2017 Georgios Katsikas, RISE SICS
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

#include <click/args.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/etheraddress.hh>
#include <click/straccum.hh>
#include <click/dpdk_glue.hh>

#include "fromdpdkdevice.hh"
#include "tscclock.hh"
#include "todpdkdevice.hh"
#include <click/dpdk_glue.hh>
#include <click/json.hh>

#if HAVE_FLOW_API
    #include <click/flowrulemanager.hh>
#endif

CLICK_DECLS

#define LOAD_UNIT 10

FromDPDKDevice::FromDPDKDevice() :
    _dev(0), _tco(false), _uco(false), _ipco(false)
#if HAVE_DPDK_INTERRUPT
    ,_rx_intr(-1)
#endif
{
#if HAVE_BATCH
    in_batch_mode = BATCH_MODE_YES;
#endif
    _burst = 32;
    ndesc = 0;
}

FromDPDKDevice::~FromDPDKDevice()
{
}

int FromDPDKDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    //Default parameters
    int numa_node = 0;
    int minqueues = 1;
    int maxqueues = 128;
    String dev;
    EtherAddress mac;
    uint16_t mtu = 0;
    bool has_mac = false;
    bool has_mtu = false;
    bool set_timestamp = false;
    FlowControlMode fc_mode(FC_UNSET);
    String mode = "";
    int num_pools = 0;
    Vector<int> vf_vlan;
    int max_rss = 0;
    bool has_rss = false;
    bool flow_isolate = false;
#if HAVE_FLOW_API
    String flow_rules_filename;
#endif
    if (Args(this, errh).bind(conf)
        .read_mp("PORT", dev)
        .consume() < 0)
        return -1;

    if (parse(conf, errh) != 0)
        return -1;

    if (Args(conf, this, errh)
        .read("NDESC", ndesc)
        .read("MAC", mac).read_status(has_mac)
        .read("MTU", mtu).read_status(has_mtu)
        .read("MODE", mode)
        .read("FLOW_ISOLATE", flow_isolate)
    #if HAVE_FLOW_API
        .read("FLOW_RULES_FILE", flow_rules_filename)
    #endif
        .read("VF_POOLS", num_pools)
        .read_all("VF_VLAN", vf_vlan)
        .read("MINQUEUES",minqueues)
        .read("MAXQUEUES",maxqueues)
#if HAVE_DPDK_INTERRUPT
        .read("RX_INTR", _rx_intr)
#endif
        .read("MAX_RSS", max_rss).read_status(has_rss)
        .read("TIMESTAMP", set_timestamp)
        .read_or_set("RSS_AGGREGATE", _set_rss_aggregate, false)
        .read_or_set("PAINT_QUEUE", _set_paint_anno, false)
        .read_or_set("BURST", _burst, 32)
        .read_or_set("CLEAR", _clear, false)
        .read("PAUSE", fc_mode)
#if RTE_VERSION >= RTE_VERSION_NUM(18,02,0,0)
        .read("IPCO", _ipco)
        .read("TCO", _tco)
        .read("UCO", _uco)
#endif
        .complete() < 0)
        return -1;

    if (!DPDKDeviceArg::parse(dev, _dev)) {
        if (allow_nonexistent)
            return 0;
        else
            return errh->error("%s: Unknown or invalid PORT", dev.c_str());
    }

    if (_use_numa) {
        numa_node = DPDKDevice::get_port_numa_node(_dev->port_id);
        if (_numa_node_override > -1)
            numa_node = _numa_node_override;
    }

    int r;
    if (n_queues == -1) {
        if (firstqueue == -1) {
            firstqueue = 0;
            // With DPDK we'll take as many queues as available threads
            r = configure_rx(numa_node, minqueues, maxqueues, errh);
        } else {
            // If a queue number is set, user probably wants only one queue
            r = configure_rx(numa_node, 1, 1, errh);
        }
    } else {
        if (firstqueue == -1)
            firstqueue = 0;
        r = configure_rx(numa_node, n_queues, n_queues, errh);
    }
    if (r != 0)
        return r;

    if (has_mac)
        _dev->set_init_mac(mac);

    if (has_mtu)
        _dev->set_init_mtu(mtu);

    if (fc_mode != FC_UNSET)
        _dev->set_init_fc_mode(fc_mode);

    if (_ipco || _tco || _uco)
        _dev->set_rx_offload(DEV_RX_OFFLOAD_IPV4_CKSUM);
    if (_tco)
        _dev->set_rx_offload(DEV_RX_OFFLOAD_TCP_CKSUM);
    if (_uco)
        _dev->set_rx_offload(DEV_RX_OFFLOAD_UDP_CKSUM);

    if (set_timestamp) {
#if RTE_VERSION >= RTE_VERSION_NUM(18,02,0,0)
        _dev->set_rx_offload(DEV_RX_OFFLOAD_TIMESTAMP);
        _set_timestamp = true;
#else
        errh->error("Hardware timestamping is not supported before DPDK 18.02");
#endif
    } else {
        _set_timestamp = false;
    }

    if (has_rss)
        _dev->set_init_rss_max(max_rss);

#if RTE_VERSION >= RTE_VERSION_NUM(18,05,0,0)
    _dev->set_init_flow_isolate(flow_isolate);
#else
    if (flow_isolate)
        return errh->error("Flow isolation needs DPDK >= 18.05. Set FLOW_ISOLATE to false");
#endif
#if HAVE_FLOW_API
    if ((mode == FlowRuleManager::DISPATCHING_MODE) && (flow_rules_filename.empty())) {
        errh->warning(
            "DPDK Flow Rule Manager (port %s): FLOW_RULES_FILE is not set, "
            "hence this NIC can only be configured by the handlers",
            dev.c_str()
        );
    }

    r = _dev->set_mode(mode, num_pools, vf_vlan, flow_rules_filename, errh);
#else
    r = _dev->set_mode(mode, num_pools, vf_vlan, errh);
#endif

    return r;
}

#if HAVE_DPDK_READ_CLOCK
uint64_t FromDPDKDevice::read_clock(void* thunk) {
    FromDPDKDevice* fd = (FromDPDKDevice*)thunk;
    uint64_t clock;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (rte_eth_read_clock(fd->_dev->port_id, &clock) == 0)
        return clock;
#pragma GCC diagnostic pop
    return -1;
}

struct UserClockSource dpdk_clock {
    .get_current_tick = &FromDPDKDevice::read_clock,
    .get_tick_hz = 0,
};
#endif

void* FromDPDKDevice::cast(const char* name) {
#if HAVE_DPDK_READ_CLOCK
    if (String(name) == "UserClockSource")
        return &dpdk_clock;
#endif
    if (String(name) == "EthernetDevice")
        return get_eth_device();
    if (String(name) == "DPDKDevice")
        return _dev;
    return RXQueueDevice::cast(name);
}

int FromDPDKDevice::initialize(ErrorHandler *errh)
{
    int ret;

    if (!_dev)
        return 0;

    ret = initialize_rx(errh);
    if (ret != 0) return ret;

    for (unsigned i = (unsigned)firstqueue; i <= (unsigned)lastqueue; i++) {
        ret = _dev->add_rx_queue(i , _promisc, _vlan_filter, _vlan_strip, _vlan_extend, _lro, _jumbo, ndesc, errh);
        if (ret != 0) return ret;
    }

    ret = initialize_tasks(_active,errh);
    if (ret != 0) return ret;

    if (queue_share > 1)
        return errh->error(
            "Sharing queue between multiple threads is not "
            "yet supported by FromDPDKDevice. "
            "Raise the number using N_QUEUES of queues or "
            "limit the number of threads using MAXTHREADS"
        );

    if (all_initialized()) {
        ret = DPDKDevice::initialize(errh);
        if (ret != 0) return ret;
    }

    if (_set_timestamp) {
#if HAVE_DPDK_READ_CLOCK
        uint64_t t;
        int err;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        if ((err = rte_eth_read_clock(_dev->port_id, &t)) != 0) {
            return errh->error("Device does not support queryig internal time ! Disable hardware timestamping. Error %d", err);
        }
#pragma GCC diagnostic pop
#endif
    }

#if HAVE_DPDK_INTERRUPT
    if (_rx_intr >= 0) {
        for (int i = firstqueue; i <= lastqueue; i++) {
            uint64_t data = _dev->port_id << CHAR_BIT | i;
            ret = rte_eth_dev_rx_intr_ctl_q(_dev->port_id, i,
                                            RTE_EPOLL_PER_THREAD,
                                            RTE_INTR_EVENT_ADD,
                                            (void *)((uintptr_t)data));
            if (ret != 0) {
                return errh->error(
                    "Cannot initialize RX interrupt on this device"
                );
            }
        }
    }
#endif

    return ret;
}

void FromDPDKDevice::cleanup(CleanupStage)
{
    DPDKDevice::cleanup(ErrorHandler::default_handler());
    cleanup_tasks();
}

void FromDPDKDevice::clear_buffers() {
    rte_mbuf* pkts[32];
    for (int q = firstqueue; q <= lastqueue; q++) {
        unsigned n;
        int tot = 0;
        do {
            n = rte_eth_rx_burst(_dev->port_id, q, pkts, 32);
            tot += n;
            for (int i = 0; i < n; i ++) {
                 rte_pktmbuf_free(pkts[i]);
            }
            if (tot > _dev->get_nb_rxdesc()) {
                click_chatter("WARNING : Called clear_buffers while receiving packets !");
                break;
            }
        } while (n > 0);
        click_chatter("Cleared %d buffers for queue %d",tot,q);
    }
}
#ifdef DPDK_USE_XCHG
extern "C" {
#include <mlx5_xchg.h>
}
#endif

bool FromDPDKDevice::run_task(Task *t) {
  struct rte_mbuf *pkts[_burst];
  int ret = 0;

  int iqueue = queue_for_thisthread_begin();
  { //This version differs from multi by having support for one queue per thread only, which is extremly usual
#if HAVE_BATCH
  PacketBatch *head = 0;
  WritablePacket *last;
#endif

#ifdef DPDK_USE_XCHG
 unsigned n = rte_mlx5_rx_burst_xchg(_dev->port_id, iqueue, (struct xchg**)pkts, _burst);
#else
 unsigned n = rte_eth_rx_burst(_dev->port_id, iqueue, pkts, _burst);
#endif

for (unsigned i = 0; i < n; ++i) {
    unsigned char *data = rte_pktmbuf_mtod(pkts[i], unsigned char *);
    rte_prefetch0(data);
#if CLICK_PACKET_USE_DPDK
    WritablePacket *p = static_cast<WritablePacket *>(Packet::make(pkts[i], _clear));
#elif HAVE_ZEROCOPY

# if CLICK_PACKET_INSIDE_DPDK
    WritablePacket *p =(WritablePacket*)( pkts[i] + 1);
    new (p) WritablePacket();

    p->initialize(_clear);
    p->set_buffer((unsigned char*)(pkts[i]->buf_addr), DPDKDevice::MBUF_DATA_SIZE);
    p->set_data(data);
    p->set_data_length(rte_pktmbuf_data_len(pkts[i]));
    p->set_buffer_destructor(DPDKDevice::free_pkt);

    p->set_destructor_argument(pkts[i]);
# else
    WritablePacket *p = Packet::make(
        data, rte_pktmbuf_data_len(pkts[i]), DPDKDevice::free_pkt, pkts[i],
        rte_pktmbuf_headroom(pkts[i]), rte_pktmbuf_tailroom(pkts[i]), _clear);
# endif
#else
            WritablePacket *p = Packet::make(data,
                                     (uint32_t)rte_pktmbuf_pkt_len(pkts[i]));
            rte_pktmbuf_free(pkts[i]);
            data = p->data();
#endif
            p->set_packet_type_anno(Packet::HOST);
            p->set_mac_header(data);
            if (_set_rss_aggregate)
#if RTE_VERSION > RTE_VERSION_NUM(1,7,0,0)
                SET_AGGREGATE_ANNO(p,pkts[i]->hash.rss);
#else
                SET_AGGREGATE_ANNO(p,pkts[i]->pkt.hash.rss);
#endif
            if (_set_paint_anno) {
                SET_PAINT_ANNO(p, iqueue);
            }

#if RTE_VERSION >= RTE_VERSION_NUM(18,02,0,0)
            if (_set_timestamp && HAS_TIMESTAMP(pkts[i])) {
                p->timestamp_anno().assignlong(TIMESTAMP_FIELD(pkts[i]));
            }
#endif
#if HAVE_BATCH
            if (head == NULL)
                head = PacketBatch::start_head(p);
            else
                last->set_next(p);
            last = p;
#else
            output(0).push(p);
#endif
        }
#if HAVE_BATCH
        if (head) {
            head->make_tail(last,n);
            output_push_batch(0,head);
        }
#endif
        if (n) {
            add_count(n);
            ret = 1;
        }
    }

#if HAVE_DPDK_INTERRUPT
     if (ret == 0 && _rx_intr >= 0) {
           for (int iqueue = queue_for_thisthread_begin();
                iqueue<=queue_for_thisthread_end(); iqueue++) {
               if (rte_eth_dev_rx_intr_enable(_dev->port_id, iqueue) != 0) {
                   click_chatter("Could not enable interrupts");
                   t->fast_reschedule();
                   return 0;
               }
           }
           struct rte_epoll_event event[queue_per_threads];
           int n, i;
           uint8_t port_id, queue_id;
           void *data;
           n = rte_epoll_wait(RTE_EPOLL_PER_THREAD, event, queue_per_threads, -1);
           for (i = 0; i < n; i++) {
                   data = event[i].epdata.data;
                   port_id = ((uintptr_t)data) >> CHAR_BIT;
                   assert(port_id == _dev->port_id);
           }
           this->selected(0, SELECT_READ);
    }
#endif

    t->fast_reschedule();
    return ret;
}

#if HAVE_DPDK_INTERRUPT
void FromDPDKDevice::selected(int fd, int mask) {
    for (int iqueue = queue_for_thisthread_begin();
            iqueue<=queue_for_thisthread_end(); iqueue++) {
        if (rte_eth_dev_rx_intr_disable(_dev->port_id, iqueue) != 0) {
            click_chatter("Could not disable interrupts");
            return;
        }
    }
    task_for_thread()->reschedule();
}
#endif

enum {
    h_vendor, h_driver, h_carrier, h_duplex, h_autoneg, h_speed, h_type,
    h_ipackets, h_ibytes, h_imissed, h_ierrors, h_nombufs,
    h_stats_packets, h_stats_bytes,
    h_active, h_safe_active,
    h_xstats, h_queue_count,
    h_nb_rx_queues, h_nb_tx_queues, h_nb_vf_pools,
    h_rss, h_rss_reta, h_rss_reta_size,
    h_mac, h_add_mac, h_remove_mac, h_vf_mac,
    h_mtu,
    h_device, h_isolate,
#if HAVE_FLOW_API
    h_rule_add, h_rules_del, h_rules_flush,
    h_rules_list, h_rules_list_with_hits, h_rules_ids_global, h_rules_ids_internal,
    h_rules_count, h_rules_count_with_hits, h_rule_packet_hits, h_rule_byte_count,
    h_rules_aggr_stats
#endif
};

String FromDPDKDevice::read_handler(Element *e, void * thunk)
{
    FromDPDKDevice *fd = static_cast<FromDPDKDevice *>(e);

    switch((uintptr_t) thunk) {
        case h_active:
              if (!fd->_dev)
                  return "false";
              else
                  return String(fd->_active);
        case h_device:
              if (!fd->_dev)
                  return "undefined";
              else
                  return String((int) fd->_dev->port_id);
        case h_nb_rx_queues:
            return String(fd->_dev->nb_rx_queues());
        case h_nb_tx_queues:
            return String(fd->_dev->nb_tx_queues());
        case h_nb_vf_pools:
            return String(fd->_dev->nb_vf_pools());
        case h_mtu: {
            uint16_t mtu;
            if (rte_eth_dev_get_mtu(fd->_dev->port_id, &mtu) != 0)
                return String("<error>");
            return String(mtu);
                    }
        case h_mac: {
            if (!fd->_dev)
                return String::make_empty();
            struct rte_ether_addr mac_addr;
            rte_eth_macaddr_get(fd->_dev->port_id, &mac_addr);
            return EtherAddress((unsigned char*)&mac_addr).unparse_colon();
        }
        case h_vf_mac: {
#if HAVE_JSON
            Json jaddr = Json::make_array();
            for (int i = 0; i < fd->_dev->nb_vf_pools(); i++) {
                struct rte_ether_addr mac = fd->_dev->gen_mac(fd->_dev->port_id, i);
                jaddr.push_back(
                    EtherAddress(
                        reinterpret_cast<unsigned char *>(&mac)
                    ).unparse_colon());
            }
            return jaddr.unparse();
#else
            String s = "";
            for (int i = 0; i < fd->_dev->nb_vf_pools(); i++) {
                struct rte_ether_addr mac = fd->_dev->gen_mac(fd->_dev->port_id, i);
                s += EtherAddress(
                        reinterpret_cast<unsigned char *>(&mac)
                    ).unparse_colon() + ";";
            }
            return s;
#endif
        }
        case h_vendor:
            return fd->_dev->get_device_vendor_name();
        case h_driver:
            return String(fd->_dev->get_device_driver());
        case h_rss_reta_size:
		    return String(fd->_dev->dpdk_get_rss_reta_size());
        case h_rss_reta:
            StringAccum acc;
            Vector<unsigned> list = fd->_dev->dpdk_get_rss_reta();
            for (int i= 0; i < list.size(); i++) {
                acc << list[i] << " ";
            }
            return acc.take_string();
    }

    return 0;
}

String FromDPDKDevice::status_handler(Element *e, void * thunk)
{
    FromDPDKDevice *fd = static_cast<FromDPDKDevice *>(e);
    struct rte_eth_link link;
    if (!fd->_dev) {
        return "0";
    }

    rte_eth_link_get_nowait(fd->_dev->port_id, &link);
#ifndef ETH_LINK_UP
    #define ETH_LINK_UP 1
#endif
    switch((uintptr_t) thunk) {
      case h_carrier:
          return (link.link_status == ETH_LINK_UP ? "1" : "0");
      case h_duplex:
          return (link.link_status == ETH_LINK_UP ?
            (link.link_duplex == ETH_LINK_FULL_DUPLEX ? "1" : "0") : "-1");
#if RTE_VERSION >= RTE_VERSION_NUM(16,04,0,0)
      case h_autoneg:
          return String(link.link_autoneg);
#endif
      case h_speed:
          return String(link.link_speed);
      case h_type:
          //TODO
          return String("fiber");
    }
    return 0;
}

String FromDPDKDevice::statistics_handler(Element *e, void *thunk)
{
    FromDPDKDevice *fd = static_cast<FromDPDKDevice *>(e);
    struct rte_eth_stats stats;
    if (!fd->_dev) {
        return "0";
    }

    if (rte_eth_stats_get(fd->_dev->port_id, &stats))
        return String::make_empty();

    switch((uintptr_t) thunk) {
        case h_ipackets:
            return String(stats.ipackets);
        case h_ibytes:
            return String(stats.ibytes);
        case h_imissed:
            return String(stats.imissed);
        case h_ierrors:
            return String(stats.ierrors);
#if RTE_VERSION >= RTE_VERSION_NUM(18,05,0,0)
        case h_isolate: {
            return String(fd->get_device()->isolated() ? "1" : "0");
        }
#endif
    #if HAVE_FLOW_API
        case h_rules_list: {
            portid_t port_id = fd->get_device()->get_port_id();
            return FlowRuleManager::get_flow_rule_mgr(port_id)->flow_rules_list();
        }
        case h_rules_list_with_hits: {
            portid_t port_id = fd->get_device()->get_port_id();
            return FlowRuleManager::get_flow_rule_mgr(port_id)->flow_rules_list(true);
        }
        case h_rules_ids_global: {
            portid_t port_id = fd->get_device()->get_port_id();
            return FlowRuleManager::get_flow_rule_mgr(port_id)->flow_rule_ids_global();
        }
        case h_rules_ids_internal: {
            portid_t port_id = fd->get_device()->get_port_id();
            return FlowRuleManager::get_flow_rule_mgr(port_id)->flow_rule_ids_internal();
        }
        case h_rules_count: {
            portid_t port_id = fd->get_device()->get_port_id();
            return String(FlowRuleManager::get_flow_rule_mgr(port_id)->flow_rules_count_explicit());
        }
        case h_rules_count_with_hits: {
            portid_t port_id = fd->get_device()->get_port_id();
            return String(FlowRuleManager::get_flow_rule_mgr(port_id)->flow_rules_with_hits_count());
        }
    #endif
        case h_nombufs:
            return String(stats.rx_nombuf);
        default:
            return "<unknown>";
    }
}

int FromDPDKDevice::write_handler(
        const String &input, Element *e, void *thunk, ErrorHandler *errh) {
    FromDPDKDevice *fd = static_cast<FromDPDKDevice *>(e);
    if (!fd->_dev) {
        return -1;
    }

    switch((uintptr_t) thunk) {
        case h_add_mac: {
            EtherAddress mac;
            int pool = 0;
            int ret;
            if (!EtherAddressArg().parse(input, mac)) {
                return errh->error("Invalid MAC address %s",input.c_str());
            }

            ret = rte_eth_dev_mac_addr_add(
                fd->_dev->port_id,
                reinterpret_cast<rte_ether_addr*>(mac.data()), pool
            );
            if (ret != 0) {
                return errh->error("Could not add mac address!");
            }
            return 0;
        }
        case h_safe_active:
        case h_active: {
            bool active;
            if (!BoolArg::parse(input,active))
                return errh->error("Not a valid boolean");
            if (fd->_active != active) {
                fd->_active = active;
                Bitvector b(fd->router()->master()->nthreads());
                fd->get_spawning_threads(b, true, -1);
                if (fd->_active) { // Activating
                    fd->trigger_thread_reconfiguration(true,[fd,thunk](){
                        for (int i = 0; i < fd->_thread_state.weight(); i++) {
                            if (fd->_thread_state.get_value(i).task)
                                fd->_thread_state.get_value(i).task->reschedule();
                        }
                        for (int q = 0; q <= fd->n_queues; q++) {
                            int i = fd->thread_for_queue_offset(q);
                        }
                    }, b);
                } else { // Deactivating
                    fd->trigger_thread_reconfiguration(false,[fd](){
                        for (int i = 0; i < fd->_thread_state.weight(); i++) {
                            if (fd->_thread_state.get_value(i).task)
                                fd->_thread_state.get_value(i).task->unschedule();
                        }

                        for (int q = 0; q <= fd->n_queues; q++) {
                            int i = fd->thread_for_queue_offset(q);
                        }
                    }, b);
                }
            }
            return 0;
        }
        case h_rss: {
            int max;
            if (!IntArg().parse<int>(input,max))
                return errh->error("Not a valid integer");
            return fd->_dev->dpdk_set_rss_max(max);
        }
#if RTE_VERSION >= RTE_VERSION_NUM(18,05,0,0)
        case h_isolate: {
            if (input.empty()) {
                return errh->error("DPDK Flow Rule Manager (port %u): Specify isolation mode (true/1 -> isolation, otherwise no isolation)", fd->_dev->port_id);
            }
            bool status = (input.lower() == "true") || (input.lower() == "1") ? true : false;
            fd->_dev->set_isolation_mode(status);
            return 0;
        }
#endif

    }
    return -1;
}

#if HAVE_FLOW_API
int FromDPDKDevice::flow_handler(
        const String &input, Element *e, void *thunk, ErrorHandler *errh)
{
    FromDPDKDevice *fd = static_cast<FromDPDKDevice *>(e);
    if (!fd->get_device()) {
        return -1;
    }

    portid_t port_id = fd->get_device()->get_port_id();
    FlowRuleManager *flow_rule_mgr = FlowRuleManager::get_flow_rule_mgr(port_id, errh);
    assert(flow_rule_mgr);

    switch((uintptr_t) thunk) {
        case h_rule_add: {
            // Trim spaces left and right
            String rule = input.trim_space().trim_space_left();

            // A '\n' must be appended at the end of this rule, if not there
            int eor_pos = rule.find_right('\n');
            if ((eor_pos < 0) || (eor_pos != rule.length() - 1)) {
                rule += "\n";
            }

            // Detect and remove unwanted components
            if (!FlowRuleManager::flow_rule_filter(rule)) {
                return errh->error("DPDK Flow Rule Manager (port %u): Invalid rule '%s'", port_id, rule.c_str());
            }

            rule = "flow create " + String(port_id) + " " + rule;

            // Parse the queue index to infer the CPU core
            String queue_index_str = FlowRuleManager::fetch_token_after_keyword((char *) rule.c_str(), "queue index");
            int core_id = atoi(queue_index_str.c_str());

            const uint32_t int_rule_id = flow_rule_mgr->flow_rule_cache()->next_internal_rule_id();
            if (flow_rule_mgr->flow_rule_install(int_rule_id, (long) int_rule_id, core_id, rule) != 0) {
                return -1;
            }

            return static_cast<int>(int_rule_id);
        }
        case h_rules_del: {
            // Trim spaces left and right
            String rule_ids_str = input.trim_space().trim_space_left();

            // Split space-separated rule IDs
            Vector<String> rules_vec = rule_ids_str.split(' ');
            const uint32_t rules_nb = rules_vec.size();
            if (rules_nb == 0) {
                return -1;
            }

            // Store these rules IDs in an array
            uint32_t rule_ids[rules_nb];
            uint32_t i = 0;
            auto it = rules_vec.begin();
            while (it != rules_vec.end()) {
                rule_ids[i++] = (uint32_t) atoi(it->c_str());
                it++;
            }

            // Batch deletion
            return flow_rule_mgr->flow_rules_delete((uint32_t *) rule_ids, rules_nb);
        }
        case h_rules_flush: {
            return flow_rule_mgr->flow_rules_flush();
        }
    }

    return -1;
}
#endif

int FromDPDKDevice::xstats_handler(
        int operation, String &input, Element *e,
        const Handler *handler, ErrorHandler *errh) {
    FromDPDKDevice *fd = static_cast<FromDPDKDevice *>(e);
    if (!fd->_dev)
        return -1;

    int op = (intptr_t)handler->read_user_data();
    switch (op) {
        case h_xstats: {
            struct rte_eth_xstat_name *names;
        #if RTE_VERSION >= RTE_VERSION_NUM(16,07,0,0)
            int len = rte_eth_xstats_get_names(fd->_dev->port_id, 0, 0);
            names = static_cast<struct rte_eth_xstat_name *>(
                malloc(sizeof(struct rte_eth_xstat_name) * len)
            );
            rte_eth_xstats_get_names(fd->_dev->port_id, names, len);
            struct rte_eth_xstat *xstats;
            xstats = static_cast<struct rte_eth_xstat *>(malloc(
                sizeof(struct rte_eth_xstat) * len)
            );
            rte_eth_xstats_get(fd->_dev->port_id,xstats,len);
            if (input == "") {
                StringAccum acc;
                for (int i = 0; i < len; i++) {
                    acc << names[i].name << "[" <<
                           xstats[i].id << "] = " <<
                           xstats[i].value << "\n";
                }

                input = acc.take_string();
            } else {
                for (int i = 0; i < len; i++) {
                    if (strcmp(names[i].name,input.c_str()) == 0) {
                        input = String(xstats[i].value);
                        return 0;
                    }
                }
                return -1;
            }
            return 0;
        #else
            input = "unsupported with DPDK < 16.07";
            return -1;
        #endif
        }
        case h_queue_count:
            if (input == "") {
                StringAccum acc;
                for (uint16_t i = 0; i < fd->_dev->nb_rx_queues(); i++) {
                    int v = rte_eth_rx_queue_count(fd->_dev->get_port_id(), i);
                    acc << "Queue " << i << ": " << v << "\n";
                }
                input = acc.take_string();
            } else {
                int v = rte_eth_rx_queue_count(fd->_dev->get_port_id(), atoi(input.c_str()));
                input = String(v);
            }
            return 0;
    #if HAVE_FLOW_API
        case h_rule_byte_count:
        case h_rule_packet_hits: {
            portid_t port_id = fd->get_device()->get_port_id();
            FlowRuleManager *flow_rule_mgr = FlowRuleManager::get_flow_rule_mgr(port_id, errh);
            assert(flow_rule_mgr);
            if (input == "") {
                return errh->error("Aggregate flow rule counters are not supported. Please specify a rule number to query");
            } else {
                const uint32_t rule_id = atoi(input.c_str());
                int64_t matched_pkts = -1;
                int64_t matched_bytes = -1;
                flow_rule_mgr->flow_rule_query(rule_id, matched_pkts, matched_bytes);
                if (op == (int) h_rule_packet_hits) {
                    input = String(matched_pkts);
                } else {
                    input = String(matched_bytes);
                }
                return 0;
            }
        }
        case h_rules_aggr_stats: {
            portid_t port_id = fd->get_device()->get_port_id();
            FlowRuleManager *flow_rule_mgr = FlowRuleManager::get_flow_rule_mgr(port_id, errh);
            assert(flow_rule_mgr);
            input = flow_rule_mgr->flow_rule_aggregate_stats();
            return 0;
        }
    #endif
        case h_stats_packets:
        case h_stats_bytes: {
            struct rte_eth_stats stats;
            if (rte_eth_stats_get(fd->_dev->port_id, &stats))
                return -1;

            int id = atoi(input.c_str());
            if (id < 0 || id > RTE_ETHDEV_QUEUE_STAT_CNTRS)
                return -EINVAL;
            uint64_t v;
            if (op == (int) h_stats_packets)
                 v = stats.q_ipackets[id];
            else
                 v = stats.q_ibytes[id];
            input = String(v);
            return 0;
        }
        default:
            return -1;
    }
}

void FromDPDKDevice::add_handlers()
{
    add_read_handler("device",read_handler, h_device);

    add_read_handler("duplex",status_handler, h_duplex);
#if RTE_VERSION >= RTE_VERSION_NUM(16,04,0,0)
    add_read_handler("autoneg",status_handler, h_autoneg);
#endif
    add_read_handler("speed",status_handler, h_speed);
    add_read_handler("carrier",status_handler, h_carrier);
    add_read_handler("type",status_handler, h_type);

    set_handler("xstats", Handler::f_read | Handler::f_read_param, xstats_handler, h_xstats);
    set_handler("queue_count", Handler::f_read | Handler::f_read_param, xstats_handler, h_queue_count);
    set_handler("queue_packets", Handler::f_read | Handler::f_read_param, xstats_handler, h_stats_packets);
    set_handler("queue_bytes", Handler::f_read | Handler::f_read_param, xstats_handler, h_stats_bytes);
#if HAVE_FLOW_API
    set_handler(FlowRuleManager::FLOW_RULE_PACKET_HITS, Handler::f_read | Handler::f_read_param, xstats_handler, h_rule_packet_hits);
    set_handler(FlowRuleManager::FLOW_RULE_BYTE_COUNT,  Handler::f_read | Handler::f_read_param, xstats_handler, h_rule_byte_count);
    set_handler(FlowRuleManager::FLOW_RULE_AGGR_STATS,  Handler::f_read | Handler::f_read_param, xstats_handler, h_rules_aggr_stats);
#endif

    add_read_handler("active", read_handler, h_active);
    add_write_handler("active", write_handler, h_active);
    add_write_handler("safe_active", write_handler, h_safe_active);
    add_read_handler("count", count_handler, h_count);
    add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);

    add_read_handler("nb_rx_queues",read_handler, h_nb_rx_queues);
    add_read_handler("nb_tx_queues",read_handler, h_nb_tx_queues);
    add_read_handler("nb_vf_pools",read_handler, h_nb_vf_pools);
    add_data_handlers("nb_rx_desc", Handler::h_read, &ndesc);

    add_read_handler("mac",read_handler, h_mac);
    add_read_handler("vendor", read_handler, h_vendor);
    add_read_handler("driver", read_handler, h_driver);
    add_write_handler("add_mac",write_handler, h_add_mac, 0);
    add_write_handler("remove_mac",write_handler, h_remove_mac, 0);
    add_read_handler("vf_mac_addr",read_handler, h_vf_mac);

    add_write_handler("max_rss", write_handler, h_rss, 0);
    add_read_handler("rss_reta",read_handler, h_rss_reta);
    add_read_handler("rss_reta_size",read_handler, h_rss_reta_size);

    add_read_handler("hw_count",statistics_handler, h_ipackets);
    add_read_handler("hw_bytes",statistics_handler, h_ibytes);
    add_read_handler("hw_dropped",statistics_handler, h_imissed);
    add_read_handler("hw_errors",statistics_handler, h_ierrors);
    add_read_handler("nombufs",statistics_handler, h_nombufs);

    add_write_handler("flow_isolate", write_handler, h_isolate, 0);
    add_read_handler ("flow_isolate", statistics_handler, h_isolate);

#if HAVE_FLOW_API
    add_write_handler(FlowRuleManager::FLOW_RULE_ADD,     flow_handler, h_rule_add,    0);
    add_write_handler(FlowRuleManager::FLOW_RULE_DEL,     flow_handler, h_rules_del,   0);
    add_write_handler(FlowRuleManager::FLOW_RULE_FLUSH,   flow_handler, h_rules_flush, 0);
    add_read_handler (FlowRuleManager::FLOW_RULE_IDS_GLB,         statistics_handler, h_rules_ids_global);
    add_read_handler (FlowRuleManager::FLOW_RULE_IDS_INT,         statistics_handler, h_rules_ids_internal);
    add_read_handler (FlowRuleManager::FLOW_RULE_LIST,            statistics_handler, h_rules_list);
    add_read_handler (FlowRuleManager::FLOW_RULE_LIST_WITH_HITS,  statistics_handler, h_rules_list_with_hits);
    add_read_handler (FlowRuleManager::FLOW_RULE_COUNT,           statistics_handler, h_rules_count);
    add_read_handler (FlowRuleManager::FLOW_RULE_COUNT_WITH_HITS, statistics_handler, h_rules_count_with_hits);
#endif

    add_read_handler("mtu",read_handler, h_mtu);
    add_data_handlers("burst", Handler::h_read | Handler::h_write, &_burst);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(userlevel dpdk QueueDevice)
EXPORT_ELEMENT(FromDPDKDevice)
ELEMENT_MT_SAFE(FromDPDKDevice)
