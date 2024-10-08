// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * fromdevice.{cc,hh} -- element reads packets live from network via pcap
 * Douglas S. J. De Couto, Eddie Kohler, John Jannotti
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2001 International Computer Science Institute
 * Copyright (c) 2005-2007 Regents of the University of California
 * Copyright (c) 2011 Meraki, Inc.
 * Copyright (c) 2012 Eddie Kohler
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
#include <sys/types.h>
#include <sys/time.h>


#if !defined(__sun)
# include <sys/ioctl.h>
#else
# include <sys/ioccom.h>
#endif
#if HAVE_NET_BPF_H
# include <net/bpf.h>
# define PCAP_DONT_INCLUDE_PCAP_BPF_H 1
#endif
#include "fromdevice.hh"
#include <click/etheraddress.hh>
#include <click/error.hh>
#include <click/straccum.hh>
#include <click/args.hh>
#include <click/glue.hh>
#include <click/packet_anno.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/userutils.hh>
#include <netinet/if_ether.h>
#ifndef _LINUX_IF_ETHER_H
# define _LINUX_IF_ETHER_H 1
#endif
#ifdef HAVE_LINUX_ETHTOOL_H
#include <linux/ethtool.h>
#endif
#if HAVE_LINUX_NETLINK_H
#include <linux/netlink.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include "fakepcap.hh"
#if HAVE_LINUX_SOCKIOS_H
#include <linux/sockios.h>
#endif

#if FROMDEVICE_ALLOW_LINUX
# include <sys/socket.h>
# include <net/if.h>
# include <features.h>
# include <linux/if_packet.h>
# if HAVE_DPDK
#  define ether_addr ether_addr_undefined
# endif
#  include <net/ethernet.h>
# if HAVE_DPDK
#  undef ether_addr
# endif
#endif

#if FROMDEVICE_ALLOW_PCAP
struct my_pcap_data {
    FromDevice* fd;
    PacketBatch* batch;
    PacketBatch* batch_err;
    Packet* batch_last;
    Packet* batch_err_last;
    int batch_count;
    int batch_err_count;
};
#endif

CLICK_DECLS

#define offset_of_base(base,derived,derived_member) ((unsigned char*)(&(reinterpret_cast<base *>(0)->derived_member)) - (unsigned char*)(base *)0)

#if HAVE_LINUX_ETHTOOL_H
static int dev_eth_set_rss_reta(EthernetDevice* eth, unsigned* reta, unsigned reta_sz) {
	FromDevice* fd = (FromDevice*)((unsigned char*)eth - offset_of_base(FromDevice,EthernetDevice,get_rss_reta_size));
	return fd->dev_set_rss_reta(reta, reta_sz);
}

static int dev_eth_get_rss_reta_size(EthernetDevice* eth) {
	FromDevice* fd = (FromDevice*)((unsigned char*)eth - offset_of_base(FromDevice,EthernetDevice,get_rss_reta_size));
	return fd->dev_get_rss_reta_size();
}
#endif

FromDevice::FromDevice()
    :
#if FROMDEVICE_ALLOW_PCAP
      _task(this),
#endif
#if FROMDEVICE_ALLOW_PCAP
      _pcap(0), _pcap_complaints(0),
#endif
      _datalink(-1), _count(0), _promisc(0), _snaplen(0)
{
#if FROMDEVICE_ALLOW_LINUX || FROMDEVICE_ALLOW_PCAP
    _fd = -1;
#endif
#if HAVE_BATCH
    in_batch_mode = BATCH_MODE_YES;
#endif
#if HAVE_LINUX_ETHTOOL_H
	set_rss_reta = &dev_eth_set_rss_reta;
	get_rss_reta_size = &dev_eth_get_rss_reta_size;
#endif
}

FromDevice::~FromDevice()
{
}

int
FromDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    bool promisc = false, outbound = false, sniffer = true, timestamp = true, active = true;
    _protocol = 0;
    _snaplen = default_snaplen;
    _headroom = Packet::default_headroom;
    _headroom += (4 - (_headroom + 2) % 4) % 4; // default 4/2 alignment
    _force_ip = false;
    _burst = 1;
    String bpf_filter, capture, encap_type;
    bool has_encap;
    if (Args(conf, this, errh)
        .read_mp("DEVNAME", _ifname)
        .read_p("PROMISC", promisc)
        .read_p("SNAPLEN", _snaplen)
        .read("SNIFFER", sniffer)
        .read("FORCE_IP", _force_ip)
        .read("METHOD", WordArg(), capture)
        .read("CAPTURE", WordArg(), capture) // deprecated
        .read("BPF_FILTER", bpf_filter)
        .read("PROTOCOL", _protocol)
        .read("OUTBOUND", outbound)
        .read("HEADROOM", _headroom)
        .read("ENCAP", WordArg(), encap_type).read_status(has_encap)
        .read("BURST", _burst)
        .read("TIMESTAMP", timestamp)
		.read("ACTIVE", active)
        .complete() < 0)
        return -1;
    if (_snaplen > 65535 || _snaplen < 14)
        return errh->error("SNAPLEN out of range");
    if (_headroom > 8190)
        return errh->error("HEADROOM out of range");
    if (_burst <= 0)
        return errh->error("BURST out of range");
    _protocol = htons(_protocol);

#if FROMDEVICE_ALLOW_PCAP
    _bpf_filter = bpf_filter;
    if (has_encap) {
        _datalink = fake_pcap_parse_dlt(encap_type);
        if (_datalink < 0)
            return errh->error("bad encapsulation type");
    }
#endif

    // set _method
    if (capture == "") {
#if FROMDEVICE_ALLOW_PCAP || FROMDEVICE_ALLOW_LINUX
# if FROMDEVICE_ALLOW_PCAP
        _method = _bpf_filter ? method_pcap : method_default;
# else
        _method = method_default;
# endif
#else
        return errh->error("cannot receive packets on this platform");
#endif
    }
#if FROMDEVICE_ALLOW_LINUX
    else if (capture == "LINUX")
        _method = method_linux;
#endif
#if FROMDEVICE_ALLOW_PCAP
    else if (capture == "PCAP")
        _method = method_pcap;
#endif
    else
        return errh->error("bad METHOD");

    if (bpf_filter && _method != method_pcap)
        errh->warning("not using METHOD PCAP, BPF filter ignored");

    _sniffer = sniffer;
    _promisc = promisc;
    _outbound = outbound;
    _timestamp = timestamp;
    _active = active;
    return 0;
}


void *
FromDevice::cast(const char *n)
{
    if (strcmp(n, "EthernetDevice") == 0)
        return static_cast<EthernetDevice*>(this);
    else
	return Element::cast(n);
}

#if FROMDEVICE_ALLOW_LINUX
int
FromDevice::open_packet_socket(String ifname, ErrorHandler *errh)
{
    int fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd == -1)
        return errh->error("%s: socket: %s", ifname.c_str(), strerror(errno));

    // get interface index
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname.c_str(), sizeof(ifr.ifr_name));
    int res = ioctl(fd, SIOCGIFINDEX, &ifr);
    if (res != 0) {
        close(fd);
        return errh->error("%s: SIOCGIFINDEX: %s", ifname.c_str(), strerror(errno));
    }
    int ifindex = ifr.ifr_ifindex;

    // bind to the specified interface.  from packet man page, only
    // sll_protocol and sll_ifindex fields are used; also have to set
    // sll_family
    sockaddr_ll sa;
    memset(&sa, 0, sizeof(sa));
    sa.sll_family = AF_PACKET;
    sa.sll_protocol = htons(ETH_P_ALL);
    sa.sll_ifindex = ifindex;
    res = bind(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (res != 0) {
        close(fd);
        return errh->error("%s: bind: %s", ifname.c_str(), strerror(errno));
    }

    // nonblocking I/O on the packet socket so we can poll
    fcntl(fd, F_SETFL, O_NONBLOCK);

    return fd;
}

int
FromDevice::set_promiscuous(int fd, String ifname, bool promisc)
{
    // get interface flags
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname.c_str(), sizeof(ifr.ifr_name));
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0)
        return -2;
    int was_promisc = (ifr.ifr_flags & IFF_PROMISC ? 1 : 0);

    // set or reset promiscuous flag
#ifdef SOL_PACKET
    if (ioctl(fd, SIOCGIFINDEX, &ifr) != 0)
        return -2;
    struct packet_mreq mr;
    memset(&mr, 0, sizeof(mr));
    mr.mr_ifindex = ifr.ifr_ifindex;
    mr.mr_type = (promisc ? PACKET_MR_PROMISC : PACKET_MR_ALLMULTI);
    if (setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) < 0)
        return -3;
#else
    if (was_promisc != promisc) {
        ifr.ifr_flags = (promisc ? ifr.ifr_flags | IFF_PROMISC : ifr.ifr_flags & ~IFF_PROMISC);
        if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0)
            return -3;
    }
#endif

    return was_promisc;
}
#endif /* FROMDEVICE_ALLOW_LINUX */

#if FROMDEVICE_ALLOW_PCAP
const char*
FromDevice::fetch_pcap_error(pcap_t* pcap, const char *ebuf)
{
    if ((!ebuf || !ebuf[0]) && pcap)
        ebuf = pcap_geterr(pcap);
    if (!ebuf || !ebuf[0])
        return "unknown error";
    else
        return ebuf;
}

pcap_t *
FromDevice::open_pcap(String ifname, int snaplen, bool promisc,
                      ErrorHandler *errh)
{
    // create pcap
    char ebuf[PCAP_ERRBUF_SIZE];
    ebuf[0] = 0;
    pcap_t* p = pcap_create(ifname.c_str(), ebuf);
    if (!p) {
        // Note: pcap error buffer will contain the interface name
        errh->error("%s: %s", ifname.c_str(), fetch_pcap_error(0, ebuf));
        return 0;
    }

    // set snaplen and promisc
    if (pcap_set_snaplen(p, snaplen))
        errh->warning("%s: error while setting snaplen", ifname.c_str());
    if (pcap_set_promisc(p, promisc))
        errh->warning("%s: error while setting promisc", ifname.c_str());

    // set timeout
    int timeout_msec = 1;
# if HAVE_PCAP_SETNONBLOCK
    // Since the socket will be made nonblocking, set the timeout higher.
    timeout_msec = 500;
# endif
    if (pcap_set_timeout(p, timeout_msec))
        errh->warning("%s: error while setting timeout", ifname.c_str());

# if TIMESTAMP_NANOSEC && defined(PCAP_TSTAMP_PRECISION_NANO)
    // request nanosecond precision
    (void) pcap_set_tstamp_precision(p, PCAP_TSTAMP_PRECISION_NANO);
# endif

# if HAVE_PCAP_SET_IMMEDIATE_MODE
    if (pcap_set_immediate_mode(p, 1))
        errh->warning("%s: error while setting immediate mode", ifname.c_str());
# endif

    // activate pcap
    int r = pcap_activate(p);
    if (r < 0) {
        errh->error("%s: %s", ifname.c_str(), fetch_pcap_error(p, 0));
        pcap_close(p);
        return 0;
    } else if (r > 0)
        errh->warning("%s: %s", ifname.c_str(), fetch_pcap_error(p, 0));

    // set nonblocking
# if HAVE_PCAP_SETNONBLOCK
    if (pcap_setnonblock(p, 1, ebuf) < 0)
        errh->warning("nonblocking %s: %s", ifname.c_str(), fetch_pcap_error(p, ebuf));
# else
    if (fcntl(pcap_fileno(p), F_SETFL, O_NONBLOCK) < 0)
        errh->warning("nonblocking %s: %s", ifname.c_str(), strerror(errno));
# endif

    return p;
}
#endif

int
FromDevice::initialize(ErrorHandler *errh)
{
    if (!_ifname)
        return errh->error("interface not set");

    if (_active) {
#if FROMDEVICE_ALLOW_PCAP
    if (_method == method_default || _method == method_pcap) {
        assert(!_pcap);
        _pcap = open_pcap(_ifname, _snaplen, _promisc, errh);
        if (!_pcap)
            return -1;
        _fd = pcap_fileno(_pcap);
        char *ifname = _ifname.mutable_c_str();

# if TIMESTAMP_NANOSEC && defined(PCAP_TSTAMP_PRECISION_NANO)
        _pcap_nanosec = false;
        if (pcap_get_tstamp_precision(_pcap) == PCAP_TSTAMP_PRECISION_NANO)
            _pcap_nanosec = true;
# endif

# if HAVE_PCAP_SETDIRECTION
        pcap_setdirection(_pcap, _outbound ? PCAP_D_INOUT : PCAP_D_IN);
# elif defined(BIOCSSEESENT)
        {
            int r, accept = _outbound;
            if ((r = ioctl(_fd, BIOCSSEESENT, &accept)) == -1)
                return errh->error("%s: BIOCSSEESENT: %s", ifname, strerror(errno));
            else if (r != 0)
                errh->warning("%s: BIOCSSEESENT returns %d", ifname, r);
        }
# endif

# if defined(BIOCIMMEDIATE) && !defined(__sun) // pcap/bpf ioctl, not in DLPI/bufmod
        {
            int r, yes = 1;
            if ((r = ioctl(_fd, BIOCIMMEDIATE, &yes)) == -1)
                return errh->error("%s: BIOCIMMEDIATE: %s", ifname, strerror(errno));
            else if (r != 0)
                errh->warning("%s: BIOCIMMEDIATE returns %d", ifname, r);
        }
# endif

        if (_datalink == -1) {  // no ENCAP specified in configure()
            _datalink = pcap_datalink(_pcap);
        } else {
            if (pcap_set_datalink(_pcap, _datalink) == -1)
                return errh->error("%s: pcap_set_datalink: %s", ifname, pcap_geterr(_pcap));
        }

        bpf_u_int32 netmask;
        bpf_u_int32 localnet;
        char ebuf[PCAP_ERRBUF_SIZE];
        ebuf[0] = 0;
        if (pcap_lookupnet(ifname, &localnet, &netmask, ebuf) < 0 || ebuf[0] != 0)
            errh->warning("%s", fetch_pcap_error(0, ebuf));

        // Later versions of pcap distributed with linux (e.g. the redhat
        // linux pcap-0.4-16) want to have a filter installed before they
        // will pick up any packets.

        // compile the BPF filter
        struct bpf_program fcode;
        if (pcap_compile(_pcap, &fcode, _bpf_filter.mutable_c_str(), 0, netmask) < 0)
            return errh->error("%s: %s", ifname, fetch_pcap_error(_pcap, 0));
        if (pcap_setfilter(_pcap, &fcode) < 0)
            return errh->error("%s: %s", ifname, fetch_pcap_error(_pcap, 0));

        _datalink = pcap_datalink(_pcap);
        if (_force_ip && !fake_pcap_dlt_force_ipable(_datalink))
            errh->warning("%s: strange data link type %d, FORCE_IP will not work", ifname, _datalink);

        _method = method_pcap;
    }
#endif


#if FROMDEVICE_ALLOW_LINUX
    if (_method == method_default || _method == method_linux) {
        _fd = open_packet_socket(_ifname, errh);
        if (_fd < 0)
            return -1;

        int promisc_ok = set_promiscuous(_fd, _ifname, _promisc);
        if (promisc_ok < 0) {
            if (_promisc)
                errh->warning("cannot set promiscuous mode");
            _was_promisc = -1;
        } else
            _was_promisc = promisc_ok;

        _datalink = FAKE_DLT_EN10MB;
        _method = method_linux;
    }
#endif

#if FROMDEVICE_ALLOW_PCAP
    if (_method == method_pcap)
        ScheduleInfo::initialize_task(this, &_task, false, errh);
#endif

#if FROMDEVICE_ALLOW_PCAP || FROMDEVICE_ALLOW_LINUX
    if (_fd >= 0 && _active)
        add_select(_fd, SELECT_READ);
#endif

    if (!_sniffer)
        if (KernelFilter::device_filter(_ifname, true, errh) < 0
#if HAVE_IP6
                || KernelFilter::device_filter6(_ifname, true, errh) < 0
#endif
                ) {
            _sniffer = true;
        }
    }
    return 0;
}

void
FromDevice::cleanup(CleanupStage stage)
{
    if (stage >= CLEANUP_INITIALIZED && !_sniffer) {
        KernelFilter::device_filter(_ifname, false, ErrorHandler::default_handler());
#if HAVE_IP6
        KernelFilter::device_filter6(_ifname, false, ErrorHandler::default_handler());
#endif
    }
#if FROMDEVICE_ALLOW_LINUX
    if (_fd >= 0 && _method == method_linux) {
        if (_was_promisc >= 0)
            set_promiscuous(_fd, _ifname, _was_promisc);
        close(_fd);
    }
#endif
#if FROMDEVICE_ALLOW_PCAP
    if (_pcap)
        pcap_close(_pcap);
    _pcap = 0;
#endif
#if FROMDEVICE_ALLOW_PCAP || FROMDEVICE_ALLOW_LINUX
    _fd = -1;
#endif
}

#if FROMDEVICE_ALLOW_PCAP
CLICK_ENDDECLS
extern "C" {
void
FromDevice_get_packet(u_char* clientdata,
                      const struct pcap_pkthdr* pkthdr,
                      const u_char* data)
{
    struct my_pcap_data *md = (struct my_pcap_data *) clientdata;
    FromDevice *fd = md->fd;
    WritablePacket *p = Packet::make(fd->_headroom, data, pkthdr->caplen, 0);
    Timestamp ts = Timestamp::uninitialized_t();
#if TIMESTAMP_NANOSEC && defined(PCAP_TSTAMP_PRECISION_NANO)
    if (fd->_pcap_nanosec)
        ts = Timestamp::make_nsec(pkthdr->ts.tv_sec, pkthdr->ts.tv_usec);
    else
#endif
        ts = Timestamp::make_usec(pkthdr->ts.tv_sec, pkthdr->ts.tv_usec);

    // set packet type annotation
    if (p->data()[0] & 1) {
        if (EtherAddress::is_broadcast(p->data()))
            p->set_packet_type_anno(Packet::BROADCAST);
        else
            p->set_packet_type_anno(Packet::MULTICAST);
    }

    // set annotations
    p->set_timestamp_anno(ts);
    p->set_mac_header(p->data());
    SET_EXTRA_LENGTH_ANNO(p, pkthdr->len - pkthdr->caplen);

#if HAVE_BATCH
    if (!fd->_force_ip || fake_pcap_force_ip(p, fd->_datalink)) {
        if (md->batch)
            md->batch_last->set_next(p);
        else
            md->batch = PacketBatch::start_head(p);
        md->batch_last = p;
        md->batch_count++;
    } else {
        if (md->batch_err)
            md->batch_err_last->set_next(p);
        else
            md->batch_err = PacketBatch::start_head(p);
        md->batch_err_last = p;
        md->batch_err_count++;
    }
#else
    if (!fd->_force_ip || fake_pcap_force_ip(p, fd->_datalink))
        fd->output(0).push(p);
    else
        fd->checked_output_push(1, p);
#endif
}
}
CLICK_DECLS
#endif

void
FromDevice::selected(int, int)
{
#if FROMDEVICE_ALLOW_PCAP
    if (_method == method_pcap) {
        struct my_pcap_data md = {this, 0, 0, 0, 0, 0, 0};
        // Read and push() at most one burst of packets.
        int r = pcap_dispatch(_pcap, _burst, FromDevice_get_packet, (u_char *) &md);
        if (r > 0) {
            _count += r;
            _task.reschedule();
#if HAVE_BATCH
            if (md.batch) {
                md.batch->make_tail(md.batch_last, md.batch_count);
                output(0).push_batch(md.batch);
            }
            if (md.batch_err) {
                md.batch_err->make_tail(md.batch_err_last, md.batch_err_count);
                checked_output_push_batch(1, md.batch_err);
            }
#endif
        } else if (r < 0 && ++_pcap_complaints < 5)
            ErrorHandler::default_handler()->error("%p{element}: %s", this, pcap_geterr(_pcap));
    }
#endif
#if FROMDEVICE_ALLOW_LINUX
    if (_method == method_linux) {
# if HAVE_BATCH
        BATCH_CREATE_INIT(batch);
        BATCH_CREATE_INIT(batch_err);
# endif
        int nlinux = 0;
        while (nlinux < _burst) {
            struct sockaddr_ll sa;
            socklen_t fromlen = sizeof(sa);
            WritablePacket *p = Packet::make(_headroom, 0, _snaplen, 0);
            int len = recvfrom(_fd, p->data(), p->length(), MSG_TRUNC, (sockaddr *)&sa, &fromlen);
            if (len > 0 && (sa.sll_pkttype != PACKET_OUTGOING || _outbound)
                && (_protocol == 0 || _protocol == sa.sll_protocol)) {
                if (len > _snaplen) {
                    assert(p->length() == (uint32_t)_snaplen);
                    SET_EXTRA_LENGTH_ANNO(p, len - _snaplen);
                } else
                    p->take(_snaplen - len);
                p->set_packet_type_anno((Packet::PacketType)sa.sll_pkttype);
#ifdef SIOCGSTAMP
                p->timestamp_anno().set_timeval_ioctl(_fd, SIOCGSTAMP);
#endif
                p->set_mac_header(p->data());
                ++nlinux;
                ++_count;
# if HAVE_BATCH
                if (!_force_ip || fake_pcap_force_ip(p, _datalink)) {
                    BATCH_CREATE_APPEND(batch, p);
                } else {
                    BATCH_CREATE_APPEND(batch_err, p);
                }
# else
                if (!_force_ip || fake_pcap_force_ip(p, _datalink))
                    output(0).push(p);
                else
                    checked_output_push(1, p);
# endif
            } else {
                p->kill();
                if (len <= 0 && errno != EAGAIN)
                    click_chatter("FromDevice(%s): recvfrom: %s", _ifname.c_str(), strerror(errno));
                break;
            }
        }
# if HAVE_BATCH
        BATCH_CREATE_FINISH(batch);
        BATCH_CREATE_FINISH(batch_err);
        if (batch)
            output(0).push_batch(batch);
        if (batch_err)
            checked_output_push_batch(1, batch_err);
# endif
    }
#endif
}

#if FROMDEVICE_ALLOW_PCAP
bool
FromDevice::run_task(Task *)
{
    // Read and push() at most one burst of packets.
    int r = 0;
    struct my_pcap_data md = {this, 0, 0, 0, 0, 0, 0};
    if (_method == method_pcap) {
        r = pcap_dispatch(_pcap, _burst, FromDevice_get_packet, (u_char *) &md);
        if (r < 0 && ++_pcap_complaints < 5)
            ErrorHandler::default_handler()->error("%p{element}: %s", this, pcap_geterr(_pcap));
    }
    if (r > 0) {
        _count += r;
        if (likely(_active))
		_task.fast_reschedule();
#if HAVE_BATCH
        if (md.batch) {
            md.batch->make_tail(md.batch_last, md.batch_count);
            output(0).push_batch(md.batch);
        }
        if (md.batch_err) {
            md.batch_err->make_tail(md.batch_err_last, md.batch_err_count);
            checked_output_push_batch(1, md.batch_err);
        }
#endif
        return true;
    } else
        return false;
}
#endif

void
FromDevice::kernel_drops(bool& known, int& max_drops) const
{
    known = false, max_drops = -1;
#if FROMDEVICE_ALLOW_PCAP
    if (_method == method_pcap) {
        struct pcap_stat stats;
        if (pcap_stats(_pcap, &stats) >= 0)
            known = true, max_drops = stats.ps_drop;
    }
#endif
#if FROMDEVICE_ALLOW_LINUX && defined(PACKET_STATISTICS)
    if (_method == method_linux) {
        struct tpacket_stats stats;
        socklen_t statsize = sizeof(stats);
        if (getsockopt(_fd, SOL_PACKET, PACKET_STATISTICS, &stats, &statsize) >= 0)
            known = true, max_drops = stats.tp_drops;
    }
#endif
}


enum {h_reset_count, h_rss_max, h_rss_reta_size, h_kernel_drops, h_count, h_encap};


String
FromDevice::read_handler(Element* e, void *thunk)
{
    FromDevice* fd = static_cast<FromDevice*>(e);
    switch ((intptr_t)thunk) {
#if HAVE_LINUX_ETHTOOL_H
    case h_rss_reta_size:
	return String(fd->dev_get_rss_reta_size());
#endif
    case h_kernel_drops: {
        int max_drops;
        bool known;
        fd->kernel_drops(known, max_drops);
        if (known)
            return String(max_drops);
        else if (max_drops >= 0)
            return "<" + String(max_drops);
        else
            return "??";
    }
    case h_encap:
        return String(fake_pcap_unparse_dlt(fd->_datalink));
    case h_count:
        return String(fd->_count);
    }
    return "<error>";
}

int
FromDevice::write_handler(const String &input, Element *e, void *thunk, ErrorHandler *errh)
{
    FromDevice* fd = static_cast<FromDevice*>(e);
    switch ((intptr_t)thunk) {
#if HAVE_LINUX_ETHTOOL_H
	case h_rss_max: {
            int max;
            if (!IntArg().parse<int>(input,max))
                return errh->error("Not a valid integer");
            Vector<unsigned> table;
            table.resize(fd->dev_get_rss_reta_size());
            for (int i = 0; i < table.size(); i++) {
                table[i] = i % max;
            }
            return fd->dev_set_rss_reta(table.data(), table.size());
        }
#endif
		case h_reset_count:
			fd->_count = 0;
			return 0;
		default:
			return -1;
    }
}

#ifdef HAVE_LINUX_ETHTOOL_H
int
FromDevice::dev_get_rss_reta_size()
{
	struct ethtool_rxfh rss_head = {0};
	int err = 0;

	/* Open control socket. */
	int fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0)
		fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);

	if (!fd) {
		click_chatter("Cannot open socket");
		return -1;
	}
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_data = (char*)&rss_head;
	rss_head.cmd = ETHTOOL_GRSSH;
	strcpy(ifr.ifr_name, _ifname.c_str());
	err = ioctl(fd, SIOCETHTOOL, &ifr);
	if (err != 0) {
		click_chatter("Bad request for %s",_ifname.c_str() );
		return err;
	}

	close(fd);

	return rss_head.indir_size;
}

int
FromDevice::dev_set_rss_reta(unsigned* reta, unsigned reta_sz)
{
	struct ethtool_rxfh rss_head = {0};
	struct ethtool_rxfh *rss = NULL;
	int err = 0;
	uint32_t indir_bytes = 0;
	uint32_t entry_size = sizeof(rss_head.rss_config[0]);

	/* Open control socket. */
	int fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0)
		fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);

	if (!fd) {
		click_chatter("Cannot open socket");
		return -1;
	}

	/*
	ring_count.cmd = ETHTOOL_GRXRINGS;
	err = send_ioctl(ctx, &ring_count);
	if (err < 0) {
		perror("Cannot get RX ring count");
		return 1;
	}*/

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_data = (char*)&rss_head;
	rss_head.cmd = ETHTOOL_GRSSH;
	strcpy(ifr.ifr_name, _ifname.c_str());
	err = ioctl(fd, SIOCETHTOOL, &ifr);

	indir_bytes = reta_sz * entry_size;


	rss = (struct ethtool_rxfh*)calloc(1, sizeof(*rss) + indir_bytes + rss_head.key_size);
	if (!rss) {
		perror("Cannot allocate memory for RX flow hash config");
		err = 1;
		goto free;
	}
	rss->cmd = ETHTOOL_SRSSH;
	rss->rss_context = 0;
	rss->hfunc = 0;
	rss->key_size = 0;
	rss->indir_size = reta_sz;
	for (unsigned i = 0; i < reta_sz; i++) {
		rss->rss_config[i] = reta[i];
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_data = (char*)rss;
	strcpy(ifr.ifr_name, _ifname.c_str());
	err = ioctl(fd, SIOCETHTOOL, &ifr);
	if (err < 0) {
		perror("Cannot set RX flow hash configuration");
		err = 1;
	}
	close(fd);

free:
	free(rss);
	return err;
}

#endif

void
FromDevice::add_handlers()
{
    add_write_handler("max_rss", write_handler, h_rss_max);
    add_read_handler("rss_reta_size", read_handler, h_rss_reta_size);
    add_read_handler("kernel_drops", read_handler, h_kernel_drops);
    add_read_handler("encap", read_handler, h_encap);
    add_read_handler("count", read_handler, h_count);
    add_write_handler("reset_counts", write_handler, h_reset_count, Handler::BUTTON);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel FakePcap KernelFilter)
EXPORT_ELEMENT(FromDevice)
