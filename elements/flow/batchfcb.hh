/*
 * batchfcb.hh - This file provides modified versions of the macros
 * provided in FastClick used to manage batches of packets. These new versions
 * can be used to pass the fcb to the function applied on the batch.
 */

#ifndef MIDDLEBOX_BATCHFCB_HH
#define MIDDLEBOX_BATCHFCB_HH

/**
 * Execute a function on each packets of a batch. The function may return
 * another packet to replace the current one. This version cannot drop !
 * Use _DROPPABLE version if the function could return null.
 */
#define EXECUTE_FOR_EACH_PACKET_FCB(fnt, fcb, batch) \
                Packet* next = ((batch != NULL)? batch->next() : NULL );\
                Packet* p = batch;\
                Packet* last = NULL;\
                for (;p != NULL;p=next,next=(p==0?0:p->next())) {\
			Packet* q = fnt(fcb, p);\
					if (q != p) {\
						if (last) {\
							last->set_next(q);\
						} else {\
							batch = static_cast<PacketBatch*>(q);\
						}\
						q->set_next(next);\
					}\
					last = q;\
				}

/**
 * Execute a function on each packet of a batch. The function may return
 * another packet, or null if the packet could be dropped.
 */
#define EXECUTE_FOR_EACH_PACKET_DROPPABLE_FCB(fnt, fcb, batch, on_drop) {\
                Packet* next = ((batch != NULL)? batch->next() : NULL );\
                Packet* p = batch;\
                Packet* last = NULL;\
                int count = batch->count();\
                for (;p != NULL;p=next,next=(p==0?0:p->next())) {\
			Packet* q = fnt(fcb, p);\
			if (q == 0) {\
				on_drop(p);\
				if (last) {\
					last->set_next(next);\
				} else {\
					batch = PacketBatch::start_head(next);\
				}\
						count--;\
						continue;\
			} else if (q != p) {\
						if (last) {\
							last->set_next(q);\
						} else {\
							batch = static_cast<PacketBatch*>(q);\
						}\
						q->set_next(next);\
					}\
					last = q;\
				}\
				if (batch) {\
					batch->set_count(count);\
					batch->set_tail(last);\
				}\
			}\

#endif
