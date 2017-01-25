/*
 * flowiploadbalancer.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/flow.hh>
#include "flowipnat.hh"

CLICK_DECLS
/*
FlowIPNAT::FlowIPNAT() : _sip(){

};

FlowIPNAT::~FlowIPNAT() {

}

int
FlowIPNAT::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .read("SIP",_sip)
               .complete() < 0)
        return -1;

    return 0;
}


int FlowIPNAT::initialize(ErrorHandler *errh) {

    return 0;
}




void FlowIPNAT::push_batch(int port, int* flowdata, PacketBatch* batch) {
    if (*flowdata == 0) {
        *flowdata = 1;
        auto ip = batch->ip_header();
        IPAddress dip = IPAddress(ip->ip_dst);
        auto th = batch->tcp_header();
        assert(th);
        NATEntry entry = NATEntry(dip,th->th_dport);
        _entry.insert(entry, );

        click_chatter("New entry");
    }

    FOR_EACH_PACKET(batch,p) {
        auto iph = p->ip_header();
        ip->ip_src = _ip;
    }

    checked_output_push_batch(*flowdata, batch);
}

int
FlowIPNATReverse::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
                .read_mp("NAT",_entry)
                .complete() < 0)
        return -1;

    return 0;
}

void FlowIPNATReverse::push_batch(int port, IPAdress* flowdata, PacketBatch* batch) {
    IPAddress mapped;
    if (*flowdata == 0) {
        auto ip = batch->ip_header();
        IPAddress sip = IPAddress(ip->ip_src);
        auto th = batch->tcp_header();
        assert(th);
        NATEntry entry = NATEntry(sip,th->th_sport);
        mapped = _entry->_map.find(entry);
        *flowdata = mapped;
        click_chatter("New NAT translation %d", mapped);
    } else {
        mapped = *flowdata;
        if (mapped == 0)
            output_push_batch(0,batch);
    }

    FOR_EACH_PACKET(batch,p) {
        auto iph = p->ip_header();
        IPAddress current = ip->ip_dst;
        iph->ip_dst = mapped;
        p->set_dst_ip_anno(revflow.saddr());
        iph->ip_hl << 2
        iph->ip_sum = 0;
        iph->ip_sum = click_in_cksum((const unsigned char *)ip, hlen);
    }

    checked_output_push_batch(*flowdata, batch);
}
*/
CLICK_ENDDECLS

