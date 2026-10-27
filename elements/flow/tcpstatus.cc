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
#include <click/packetbatch.hh>

CLICK_DECLS

int
TCPStatus::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
		.read_or_set_p("SYNACK", _accept_synack, true)
	 .complete() < 0)
	return -1;

    return 0;
}

void TCPStatus::push_flow(int port, TCPStatusFlowData* flowdata, PacketBatch* head) {
	if (flowdata->status == TCP_DROP) {
		checked_output_push_batch(1,head);
		return;
	} else if (flowdata->status == TCP_ESTABLISHED) {
		output_push_batch(0,head);
		return;
	}

	auto fnt = [this,&flowdata](Packet* p) -> Packet* {
		switch(flowdata->status) {
			case TCP_FIRSTSEEN:
			//if syn and either accept synack or packet is not ack
				if ((p->tcp_header()->th_flags & TH_SYN) && (_accept_synack || (!(p->tcp_header()->th_flags & TH_ACK)))) {
					//click_chatter("%s : First packet is effectively SYN",name().c_str());
					flowdata->status = TCP_ESTABLISHED;
				} else {
					//click_chatter("%s : First packet is not SYN!",name().c_str());
					goto drop_packet;

				}
				return p;
			break;
			case TCP_DROP:
				p->kill();
				return 0;
			case TCP_ESTABLISHED:
				return p;
			break;
		}
		assert(false);
		drop_packet:
		flowdata->status = TCP_DROP;
		checked_output_push_batch(1,PacketBatch::make_from_packet(p));
		return 0;
	};
	EXECUTE_FOR_EACH_PACKET_DROPPABLE(fnt,head,[](Packet* p){});
	if (head)
		output_push_batch(0,head);

}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(TCPStatus)
