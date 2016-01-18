/*
 * tcpreorder.{cc,hh}
 */


#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/packet_anno.hh>
#include "tcpreorder.hh"
#include "clicknet/tcp.h"
#include <click/master.hh>

CLICK_DECLS

int
TCPReorder::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
  	 .complete() < 0)
    	return -1;

    return 0;
}

PacketBatch* TCPReorder::reorder_list(PacketBatch* batch) {
	//As they are nearly always sorted, we can bubble sort. THat's convenient for linked list
		Packet* STOP_AT = NULL;
		Packet* head = batch->begin();
		bool swapped;
		do {
			Packet* previous = head;
			Packet* packet = head->next();

			swapped = false;

			while (packet != STOP_AT) {
				if (ntohl(packet->tcp_header()->th_seq) < ntohl(previous->tcp_header()->th_seq)) {
					Packet* next = packet->next();
					Packet* current = packet;
					packet = previous;
					if (previous == head) {
						head = current;
					}
					previous = current;

					previous->set_next(packet);
					packet->set_next(next);
					swapped = true;
				}
				previous = packet;
				packet = packet->next();
			}
			STOP_AT = previous;
			if (!swapped) break;
		} while (swapped);
		return PacketBatch::make_from_list(head,batch->count());
}




void TCPReorder::push_batch(int port, TCPReorderFlowData* flowdata, PacketBatch* p) {
	uint32_t awaiting_seq; //Next sequence number we are waiting for

	bool donotrestart_timer=false;

	if (flowdata->waiting_for_new != NULL) {

		//We put old packets at the beginning
		Packet* pt = flowdata->waiting_for_new;
		while (pt->next()!=NULL) {
			pt = pt->next();
		}
		pt->set_next(p);
		p = flowdata->waiting_for_new;

		//If we had packet previously seen in the flow, we already have an "awaiting packet mechanism"
		awaiting_seq = flowdata->awaiting_seq;
		click_chatter("Awaiting %u",awaiting_seq);

		donotrestart_timer = true; //We don't want the packet in the list to wait forever !
	} else {
		click_chatter("New packet !");
		awaiting_seq = ntohl(p->tcp_header()->th_seq);
	}

	//First reorder packet inside the batch by seq number
	p = reorder_list(p);



	PacketBatch* p_saved = p;



	int count;

	Packet* current = p;
	while (current != NULL) {

		count++;

		//If we have a hole
		if (ntohl(current->tcp_header()->th_seq) != awaiting_seq) {

			click_chatter("Received seq %u, waited seq %u",ntohl(current->tcp_header()->th_seq), awaiting_seq);

			flowdata->awaiting_seq = awaiting_seq;


			if (current != p_saved) {
				PacketBatch* second = NULL;
				p_saved->cut(current,count,second);
				flowdata->waiting_for_new = second;

				goto end;
			} else {
				flowdata->waiting_for_new = p_saved;
				return;
			}
			/*flow->lastseen = Timestamp::now_steady();
			flow->flags |= FLOW_TIMEOUT;
			flow->flags |= (_timeout & FLOW_TIMEOUT_MASK);*/

			//flow->use_count++;


			/*if (!_timer.scheduled())
				_timer.schedule_after(Timestamp::make_usec(_timeout));*/
		} else {

			awaiting_seq = ntohl(current->tcp_header()->th_seq) + current->transport_length() - ((current->tcp_header()->th_off) * 4);
			if (current->tcp_header()->th_flags & (TH_FIN | TH_SYN)) awaiting_seq+=1;

			current = current -> next();
		}
	}
	/*if (donotrestart_timer) //There were packet waiting
		flow->use_count--;*/

	end:

	output_push_batch(port, p_saved);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(TCPReorder)
