/*
 * tcpStatus.{cc,hh}
 */


#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/packet_anno.hh>
#include "tcpstatus.hh"
#include "clicknet/tcp.h"
#include <click/master.hh>

CLICK_DECLS

int
TCPStatus::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	 .complete() < 0)
	return -1;

    return 0;
}

void TCPStatus::push_flow(int port, TCPStatusFlowData* flowdata, PacketBatch* head) {
	if (flowdata->status == TCP_DROP) {
		head->kill();
		return;
	}

	FOR_EACH_PACKET_SAFE(head,p) {
		switch(flowdata->status) {
			case TCP_FIRSTSEEN:
				if (!((p->tcp_header()->th_flags & TH_SYN) && ((p->tcp_header()->th_flags & TH_ACK) == 0))) {
					click_chatter("%s : First packet is not SYN!",name().c_str());
					goto drop_packet;
				} else {
					click_chatter("%s : First packet is effectively SYN",name().c_str());
				}
			break;
			case TCP_DROP:
				p->kill();
			break;
		}
		continue;
		drop_packet:
		flowdata->status = TCP_DROP;
		p->kill();
		continue;
	}
	output_push_batch(0,head);
	return;
	drop_flow:
	flowdata->status = TCP_DROP;
	return;
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(TCPStatus)
