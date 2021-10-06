/*
 * flowipoutputcombo.{cc,hh} -- IP router output combination element
 */

#include <click/config.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/packet_anno.hh>
#include "flowipoutputcombo.hh"
CLICK_DECLS

FlowIPOutputCombo::FlowIPOutputCombo()
{
	in_batch_mode = BATCH_MODE_NEEDED;
}

FlowIPOutputCombo::~FlowIPOutputCombo()
{
}

int
FlowIPOutputCombo::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return Args(conf, this, errh)
	.read_mp("COLOR", _color)
	.read_mp("IPADDR", _my_ip)
	.read_mp("MTU", _mtu).complete();
}

inline int FlowIPOutputCombo::action(Packet* &p_in, bool color, int given_color) {
	 int do_cksum = 0;
	  int problem_offset = -1;

	  // DropBroadcasts
	  if (p_in->packet_type_anno() == Packet::BROADCAST || p_in->packet_type_anno() == Packet::MULTICAST) {
	    return -1;
	  }

	  // PaintTee
	  if (color && given_color == _color) {
		  click_chatter("%s : Paint out %d %p ip 0x%08x -> 0x%08x, shared() = %d",name().c_str(), _color, p_in,p_in->ip_header()->ip_src,p_in->ip_header()->ip_dst,p_in->shared());
		  return 1;
	  }

	  // IPGWOptions
	  WritablePacket *p = p_in->uniqueify();
	  assert(p->has_network_header());
	  click_ip *ip = p->ip_header();
	  unsigned hlen = (ip->ip_hl << 2);

	  if (hlen > sizeof(click_ip)) {
	    uint8_t *woa = p->network_header();
	    int hlen = p->network_header_length();

	    int oi;
	    for (oi = sizeof(click_ip); oi < hlen; ) {
	      // handle one-byte options
	      unsigned type = woa[oi];
	      if (type == IPOPT_NOP) {
		oi++;
		continue;
	      } else if (type == IPOPT_EOL)
		/* end of option list */
		break;

	      // otherwise, get option length
	      int xlen = woa[oi + 1];
	      if (xlen < 2 || oi + xlen > hlen) {
		// bad length
		problem_offset = oi + 1; // to point at length
		goto ipgw_send_error;
	      } else if (type != IPOPT_RR && type != IPOPT_TS) {
		// not for us to process
		oi += xlen;
		continue;
	      }

	      // have a writable packet already

	      if(type == IPOPT_RR){
		/*
		 * Record Route.
		 * Apparently the pointer (oa[oi+2]) is 1-origin.
		 */
		int p = woa[oi+2] - 1;
		if (p >= 3 && p + 4 <= xlen) {
		  memcpy(woa + oi + p, &_my_ip, 4);
		  woa[oi+2] += 4;
		  do_cksum = 1;
		} else if (p != xlen) {
		  problem_offset = oi + 2;
		  goto ipgw_send_error;
		}
	      } else if(type == IPOPT_TS){
		/*
		 * Timestamp Option.
		 * We can't do a good job with the pre-specified mode (flg=3),
		 * since we don't know all our i/f addresses.
		 */
		int p = woa[oi+2] - 1;
		int oflw = woa[oi+3] >> 4;
		int flg = woa[oi+3] & 0xf;
		bool overflowed = 0;

		Timestamp now = Timestamp::now();
		int ms = htonl((now.sec() % 86400)*1000 + now.msec());

		if(p < 4){
		  problem_offset = oi + 2;
		  goto ipgw_send_error;
		} else if(flg == 0){
		  /* 32-bit timestamps only */
		  if(p+4 <= xlen){
		    memcpy(woa + oi + p, &ms, 4);
		    woa[oi+2] += 4;
		    do_cksum = 1;
		  } else
		    overflowed = 1;
		} else if(flg == 1){
		  /* ip address followed by timestamp */
		  if(p+8 <= xlen){
		    memcpy(woa + oi + p, &_my_ip, 4);
		    memcpy(woa + oi + p + 4, &ms, 4);
		    woa[oi+2] += 8;
		    do_cksum = 1;
		  } else
		    overflowed = 1;
		} else if (flg == 3 && p + 8 <= xlen) {
		  /* only if it's my address */
		  if (memcmp(woa + oi + p, &_my_ip, 4) == 0) {
		    memcpy(woa + oi + p + 4, &ms, 4);
		    woa[oi+2] += 8;
		    do_cksum = 1;
		  }
		} else {
		  problem_offset = oi + 3;
		  goto ipgw_send_error;
		}
		if (overflowed) {
		  if (oflw < 15) {
		    woa[oi+3] = ((oflw + 1) << 4) | flg;
		    do_cksum = 1;
		  } else {
		    problem_offset = oi + 3;
		    goto ipgw_send_error;
		  }
		}
	      }

	      oi += xlen;
	    }
	  }

	  // FixIPSrc
	  if (FIX_IP_SRC_ANNO(p)) {
	    SET_FIX_IP_SRC_ANNO(p, 0);
	    ip->ip_src = _my_ip;
	    do_cksum = 1;

	  }

	  // IPGWOptions / FixIPSrc
	  if (do_cksum) {
	    ip->ip_sum = 0;
	    ip->ip_sum = click_in_cksum(p->data(), hlen);
	  }

	  // DecIPTTL
	  if (ip->ip_ttl <= 1) {
	    return 3;
	  } else {
	    ip->ip_ttl--;
	    // 19.Aug.1999 - incrementally update IP checksum as suggested by SOSP
	    // reviewers, according to RFC1141 and RFC1624
	    unsigned long sum = (~ntohs(ip->ip_sum) & 0xFFFF) + 0xFEFF;
	    ip->ip_sum = ~htons(sum + (sum >> 16));
	  }

	  // Fragmenter
	  if (p->length() > _mtu) {
		  click_chatter("Frag %d",p->length());
		  // expect a Fragmenter there
	    return 4;
	  }

	  return 0;

	 ipgw_send_error:
	 click_chatter("IPGW");
	  SET_ICMP_PARAMPROB_ANNO(p, problem_offset);
	  return 2;
}

void FlowIPOutputCombo::push_flow(int, int* flow_data, PacketBatch * head) {
  Packet* cur = NULL;
  Packet* next = head->first();
  Packet* last = NULL;
  int o;
  int count = 0;
  while (next != NULL) {
		cur = next;
		next = cur->next();

		Packet* old_cur = cur;

		o = action(cur, 1, *flow_data);

		if (old_cur != cur) { //If packet has changed (due to expensive uniqueify)
			click_chatter("Packet has changed");
			if (last) {
				last->set_next(cur);
			}
		}

		if (o == 1) {
			PacketBatch* clone = PacketBatch::make_from_packet(cur->clone());
			clone->tail()->set_next(NULL);
			output_push_batch(1,clone);
			o = action(cur, false, *flow_data);
		}
		if (o != 0) {//An error occured
			if (last == NULL) { //We are head

			} else {
				last->set_next(next);
			}
			cur->set_next(NULL);
			output_push_batch(o,PacketBatch::make_from_packet(cur));

		} else {
			if (last == NULL) {
				head = PacketBatch::start_head(head->first());
			}
			last = cur;
			count++;
		}
  } //end while

  if (last != NULL) {
	  head->make_tail(last,count);
	  output_push_batch(0,head);
  }

  /* Packet* next = head;
 +      while (next != NULL) {
 +          Packet* nnext = next->next();
 +          next->set_next(NULL);
 +          int o = action(next, 1);
 +
 +              if (o == 1) {
 +                  PacketBatch* n = PacketBatch::make_from_packet(next->clone());
 +                  n->set_next(NULL);
 +                  output_push_batch(1,n);
 +                  o = action(next, 0);
 +              }
 +
 +              output_push_batch(o,PacketBatch::make_from_packet(next));
 +              next = nnext;
 +      }*/

}


CLICK_ENDDECLS
EXPORT_ELEMENT(FlowIPOutputCombo)
ELEMENT_MT_SAFE(FlowIPOutputCombo)
