/*
 * flowclassifier.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/routervisitor.hh>
#include <click/flowelement.hh>
#include "flowclassifier.hh"
#include "flowdispatcher.hh"

CLICK_DECLS
/*
class FlowBufferVisitor2 : public RouterVisitor {
public:
    size_t data_size;
    FlowClassifier* _classifier;
    static int shared_position[NR_SHARED_FLOW];


    FlowBufferVisitor2(FlowClassifier* classifier,size_t myoffset) : data_size(myoffset), _classifier(classifier), map(64,false) {

        map.set_range(0,myoffset,1);
    }


    bool visit(Element *e, bool isoutput, int port,
                   Element *from_e, int from_port, int distance) {
        VirtualFlowBufferElement* fbe = dynamic_cast<VirtualFlowBufferElement*>(e);

        if (fbe != NULL) { //The visited element is an element that need FCB space

            //Resize the map if needed
            if (fbe->flow_data_offset() + fbe->flow_data_size() > map.size()) map.resize(map.size() * 2);

            if (fbe->flow_data_offset() != -1) { //If flow already have some classifier
                if (fbe->flow_data_offset() >= data_size) {
                    data_size = fbe->flow_data_offset() + fbe->flow_data_size();
                } else {
                    if (map.weight_range(fbe->flow_data_offset(),fbe->flow_data_size()) > 0 && !(fbe->flow_data_index() >= 0 && shared_position[fbe->flow_data_index()] == fbe->flow_data_offset())) {

                    }
                    if (data_size < fbe->flow_data_offset() + fbe->flow_data_size()) {
                        data_size = fbe->flow_data_offset() + fbe->flow_data_size();
                    }
                }
            } else {
                if (fbe->flow_data_index() >= 0) {
                    if (shared_position[fbe->flow_data_index()] == -1) {
                        fbe->_flow_data_offset = data_size;
                        shared_position[fbe->flow_data_index()] = data_size;
                        data_size += fbe->flow_data_size();
                    } else {
                        fbe->_flow_data_offset = shared_position[fbe->flow_data_index()];
                    }
                } else {
                    fbe->_flow_data_offset = data_size;
                    data_size += fbe->flow_data_size();
                }
                fbe->_classifier = _classifier;
            }
            map.set_range(fbe->flow_data_offset(),fbe->flow_data_size(),true);
#if DEBUG_FLOW
            click_chatter("Adding %d bytes for %s at %d",fbe->flow_data_size(),e->name().c_str(),fbe->flow_data_offset());
#endif
        }
        return true;
    }
};
*/
class FlowBufferVisitor : public RouterVisitor {
public:
	size_t data_size;
	FlowClassifier* _classifier;
	static int shared_position[NR_SHARED_FLOW];
	Bitvector map;

	FlowBufferVisitor(FlowClassifier* classifier,size_t myoffset) : data_size(myoffset), _classifier(classifier), map(64,false) {
		map.set_range(0,myoffset,1);
	}


	bool visit(Element *e, bool isoutput, int port,
			       Element *from_e, int from_port, int distance) {
		VirtualFlowBufferElement* fbe = dynamic_cast<VirtualFlowBufferElement*>(e);

		if (fbe != NULL) { //The visited element is an element that need FCB space

			//Resize the map if needed
			if (fbe->flow_data_offset() + fbe->flow_data_size() > map.size()) map.resize(map.size() * 2);

			if (fbe->flow_data_offset() != -1) { //If flow already have some classifier
				if (fbe->flow_data_offset() >= data_size) {
					data_size = fbe->flow_data_offset() + fbe->flow_data_size();
				} else {
					if (map.weight_range(fbe->flow_data_offset(),fbe->flow_data_size()) > 0 && !(fbe->flow_data_index() >= 0 && shared_position[fbe->flow_data_index()] == fbe->flow_data_offset())) {
					    //TODO !
						/*click_chatter("ERROR : multiple assigner can assign flows for %s at overlapped positions, "
											  "use the RESERVE parameter to provision space.",e->name().c_str());
						click_chatter("Vector : %s, trying offset %d length %d",map.unparse().c_str(),fbe->flow_data_offset(), fbe->flow_data_size());
						e->router()->please_stop_driver();*/
					}
					if (data_size < fbe->flow_data_offset() + fbe->flow_data_size()) {
						data_size = fbe->flow_data_offset() + fbe->flow_data_size();
					}
				}
			} else {
				if (fbe->flow_data_index() >= 0) {
					if (shared_position[fbe->flow_data_index()] == -1) {
						fbe->_flow_data_offset = data_size;
						shared_position[fbe->flow_data_index()] = data_size;
						data_size += fbe->flow_data_size();
					} else {
						fbe->_flow_data_offset = shared_position[fbe->flow_data_index()];
					}
				} else {
					fbe->_flow_data_offset = data_size;
					data_size += fbe->flow_data_size();
                    click_chatter("%s from %d to %d",e->name().c_str(),fbe->_flow_data_offset,data_size);
				}
				fbe->_classifier = _classifier;
			}
			map.set_range(fbe->flow_data_offset(),fbe->flow_data_size(),true);
#if DEBUG_FLOW
			click_chatter("Adding %d bytes for %s at %d",fbe->flow_data_size(),e->name().c_str(),fbe->flow_data_offset());
#endif
		}
		return true;
	}
};

FlowClassifier::FlowClassifier(): _aggcache(false), _cache(), _pull_burst(0), _verbose(false) {
	in_batch_mode = BATCH_MODE_NEEDED;
}

FlowClassifier::~FlowClassifier() {

}

int
FlowClassifier::configure(Vector<String> &conf, ErrorHandler *errh)
{
	int reserve = 0;

    if (Args(conf, this, errh)
    		.read_p("AGGCACHE",_aggcache)
    		.read_p("RESERVE",reserve)
			.read("VERBOSE",_verbose)
			.complete() < 0)
    	return -1;

    FlowBufferVisitor v(this, sizeof(FlowNodeData) + reserve);
    router()->visit(this,true,-1,&v);
    //click_chatter("%s : pool size %d",name().c_str(),v.data_size);
    _table.get_pool().initialize(v.data_size);
    return 0;
}


void release_subflow(FlowControlBlock* fcb) {
	click_chatter("Release fcb %p idx %d",fcb,fcb->data_64[0]);
	if (fcb->parent == NULL) return;

	//Parent of the fcb is release_ptr
	FlowNode* child = static_cast<FlowNode*>(fcb->parent);
	child->release_child(FlowNodePtr(fcb));

	FlowNode* parent = child->parent();
	//click_chatter("Parent is %p, child num = %d",parent,child->getNum());
	while (parent && parent->child_deletable() && parent->getNum() == 1 && parent->default_ptr()->ptr == 0) {
		child = parent;
		parent = child->parent();
		parent->release_child(FlowNodePtr(child));
	};
}



int FlowClassifier::initialize(ErrorHandler *errh) {

	//click_chatter("%s : %d %d",name().c_str(),ninputs(),noutputs());
	//click_chatter("Pull : %d Push : %d",input_is_pull(0),input_is_push(0));
	if (input_is_pull(0)) {
		assert(input(0).element());
	}
    _table.get_pool().compress(get_passing_threads());

    FlowNode* table = FlowElementVisitor::get_downward_table(this, 0);

    if (!table)
       return errh->error("%s: FlowClassifier without any downward dispatcher?",name().c_str());

    _table.set_root(table->optimize());
	_table.get_root()->check();
    click_chatter("Table of %s after optimization :",name().c_str());
    _table.get_root()->print();
    _table.set_release_fnt(release_subflow);

    if (_verbose)
    	_table.get_root()->print();

    for (int i = 0; i < _cache.weight(); i++) {
        _cache.get_value(i) = (FlowControlBlock**)CLICK_LALLOC(sizeof(FlowControlBlock*) * 8192);
        bzero(_cache.get_value(i),sizeof(FlowControlBlock*) * 8192);
    }
	return 0;
}

inline FlowControlBlock* FlowClassifier::get_cache_fcb(Packet* p,uint32_t agg) {

		FlowControlBlock* fcb;
		int hash = agg & 0xfff;
		fcb = _cache.get()[hash];
		if (fcb && !fcb->released()) {
			if (_table.reverse_match(fcb, p)) {
				//OK
			} else {
				FlowControlBlock* firstfcb = fcb;
				int sechash = (agg >> 12) & 0xfff;
				fcb = _cache.get()[sechash + 4096];
				if (fcb && !fcb->released()) {
					if (_table.reverse_match(fcb, p)) {

					} else {
						click_chatter("Double collision for hash %d!",hash);
						fcb = _table.match(p);
						if (_cache.get()[hash]->lastseen < fcb->lastseen)
							_cache.get()[hash] = fcb;
						else
							_cache.get()[sechash] = fcb;
					}
				} else {
					fcb = _table.match(p);
					_cache.get()[sechash] = fcb;
				}
			}
		} else {
			fcb = _table.match(p);
			_cache.get()[hash] = fcb;

		}
		return fcb;

}

inline  void FlowClassifier::push_batch_simple(int port, PacketBatch* batch) {
	PacketBatch* awaiting_batch = NULL;
	Packet* p = batch;
	Packet* last = NULL;
	uint32_t lastagg = 0;


	int count =0;
	FlowControlBlock* fcb = 0;
	while (p != NULL) {
#if DEBUG_CLASSIFIER > 1
		click_chatter("Packet %p in %s",p,name().c_str());
#endif
		Packet* next = p->next();


		if (_aggcache) {
			uint32_t agg = AGGREGATE_ANNO(p);
			if (!(lastagg == agg && fcb && _table.reverse_match(fcb,p)))
				fcb = get_cache_fcb(p,agg);
		}
		else
		{
			fcb = _table.match(p);
		}

		if (awaiting_batch == NULL) {
			fcb_stack = fcb;
			awaiting_batch = batch;
		} else {
			if (fcb == fcb_stack) {
				//Do nothing as we follow the LL
			} else {
				fcb_stack->acquire(count);
				PacketBatch* new_batch = NULL;
				awaiting_batch->cut(last,count,new_batch);
				fcb->lastseen = p->timestamp_anno();
				output_push_batch(port,awaiting_batch);
				awaiting_batch = new_batch;
				fcb_stack = fcb;
				count = 0;
			}
		}

		count ++;

		last = p;
		p = next;
	}

	if (awaiting_batch) {
		fcb_stack->acquire(count);
		output_push_batch(port,awaiting_batch);
		fcb_stack = 0;
	}
}

typedef struct {
	PacketBatch* batch;
	FlowControlBlock* fcb;
} FlowBatch;

#define RING_SIZE 32

inline void FlowClassifier::push_batch_builder(int port, PacketBatch* batch) {
	Packet* p = batch;
	int curbatch = -1;
	Packet* last = NULL;
	FlowControlBlock* fcb = 0;
	FlowControlBlock* lastfcb = 0;
	uint32_t lastagg = 0;
	int count = 0;

	FlowBatch batches[RING_SIZE] = {0};
	int head = 0;
	int tail = 0;


	process:




	//click_chatter("Have %d packets.",batch->count());

	while (p != NULL) {
#if DEBUG_CLASSIFIER > 1
		click_chatter("Packet %p in %s",p,name().c_str());
#endif
		Packet* next = p->next();

		if (_aggcache) {
			uint32_t agg = AGGREGATE_ANNO(p);
			if (!(lastagg == agg && fcb && _table.reverse_match(fcb,p)))
				fcb = get_cache_fcb(p,agg);
		}
		else
		{
			fcb = _table.match(p);
		}
		if (_verbose)
			_table.get_root()->print();
		//click_chatter("p %p fcb %p - tail %d / curbatch %d / head %d",p,fcb,tail,curbatch,head);
		if (lastfcb == fcb) {
			//Just continue as they are still linked
		} else {

				//Break the last flow
				if (curbatch >= 0) {
					PacketBatch* new_batch = NULL;
					batches[curbatch].batch->cut(last,count,new_batch);
					batch = new_batch;
					count = 0;
				}

				//Find a potential match
				for (int i = tail; i < head; i++) {
					if (batches[i % RING_SIZE].fcb == fcb) { //Flow already in list, append
						//click_chatter("Flow already in list");
						curbatch = i % RING_SIZE;
						batches[curbatch].batch->tail()->set_next(batch);
						count = batches[curbatch].batch->count();
						goto attach;
					}
				}
				//click_chatter("Unknown fcb %p, curbatch = %d",fcb,head);
				curbatch = head % RING_SIZE;
				head++;

				if (tail % RING_SIZE == head % RING_SIZE) {
					click_chatter("Ring full, processing now !");
					//Ring full, process batch NOW
					fcb_stack = batches[tail % RING_SIZE].fcb;
					fcb_stack->acquire(batches[tail % RING_SIZE].batch->count());
					fcb_stack->lastseen = batches[tail % RING_SIZE].batch->tail()->timestamp_anno();
					//click_chatter("FPush %d of %d packets",tail % RING_SIZE,batches[tail % RING_SIZE].batch->count());
					output_push_batch(port,batches[tail % RING_SIZE].batch);
					tail++;
				}
				//click_chatter("batches[%d].batch = %p",curbatch,batch);
				//click_chatter("batches[%d].fcb = %p",curbatch,fcb);
				batches[curbatch].batch = batch;
				batches[curbatch].fcb = fcb;
		}
		attach:
		count ++;
		last = p;
		p = next;
		lastfcb = fcb;
	}

	batches[curbatch].batch->set_tail(last);
	batches[curbatch].batch->set_count(count);

	//click_chatter("%d batches :",head-tail);
	for (int i = tail;i < head;i++) {
		//click_chatter("[%d] %d packets for fcb %p",i,batches[i%RING_SIZE].batch->count(),batches[i%RING_SIZE].fcb);
	}
	for (;tail < head;) {
		//click_chatter("%d: batch %p of %d packets with fcb %p",i,batches[i].batch,batches[i].batch->count(),batches[i].fcb);
		fcb_stack = batches[tail % RING_SIZE].fcb;
		fcb_stack->acquire(batches[tail % RING_SIZE].batch->count());
		fcb_stack->lastseen = batches[tail % RING_SIZE].batch->tail()->timestamp_anno();
		//click_chatter("EPush %d of %d packets",tail % RING_SIZE,batches[tail % RING_SIZE].batch->count());
		output_push_batch(port,batches[tail % RING_SIZE].batch);

		curbatch = -1;
		lastfcb = 0;
		count = 0;
		lastagg = 0;

		tail++;
		if (tail == head) break;

		if (input_is_pull(0) && ((batch = input_pull_batch(0,_pull_burst)) != 0)) { //If we can pull and it's not the last pull
			//click_chatter("Pull continue because received %d ! tail %d, head %d",batch->count(),tail,head);
			p = batch;
			goto process;
		}
	}
	//click_chatter("End");

}


void FlowClassifier::push_batch(int port, PacketBatch* batch) {
	push_batch_builder(port,batch);
	//push_batch_simple(port,batch);
}


int FlowBufferVisitor::shared_position[NR_SHARED_FLOW] = {-1};

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowClassifier)
