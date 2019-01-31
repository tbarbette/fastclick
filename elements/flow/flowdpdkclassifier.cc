#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include "flowdpdkclassifier.hh"
#include <click/flow.hh>
#include <click/flow_nodes.hh>

CLICK_DECLS


FlowDPDKClassifier::FlowDPDKClassifier() {

}

FlowDPDKClassifier::~FlowDPDKClassifier() {
    
}

int FlowDPDKClassifier::configure(Vector<String> &conf, ErrorHandler *errh) {

    Element* dev;
    if (Args(this, errh).bind(conf)
            .read_mp("DEVICE", dev)
            //.validate_p("AGGCACHE", _aggcache, false)
            .consume() < 0)
        return -1;

    if (FlowClassifier::configure(conf, errh) != 0)
        return -1;

    _dev = dynamic_cast<FromDPDKDevice*>(dev);

    return 0;
}

void FlowDPDKClassifier::add_rule(Vector<rte_flow_item> pattern, FlowNodePtr ptr) {
    if (pattern.size() == 0) {
        click_chatter("Cannot add classifier without rules !");
        return;
    }
    _matches.push_back(ptr);

    int port_id = _dev->port_id();

    struct rte_flow_attr attr;
    /*
     * set the rule attribute.
     * in this case only ingress packets will be checked.
     */
    memset(&attr, 0, sizeof(struct rte_flow_attr));
    attr.ingress = 1;

    struct rte_flow_action action[3];
    struct rte_flow_action_mark mark;
    struct rte_flow_action_rss rss;

    memset(action, 0, sizeof(action));
    memset(&rss, 0, sizeof(rss));

    if (ptr.is_leaf() && ptr.leaf->is_early_drop()) {
        action[0].type = RTE_FLOW_ACTION_TYPE_DROP;
        action[1].type = RTE_FLOW_ACTION_TYPE_END;
    } else {

    action[0].type = RTE_FLOW_ACTION_TYPE_MARK;
    mark.id = _matches.size();
    action[0].conf = &mark;

    struct rte_flow_action_queue queue;
    action[1].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    queue.index = 0;
    action[1].conf = &queue;
/*
    action[1].type = RTE_FLOW_ACTION_TYPE_RSS;
    uint16_t queue[RTE_MAX_QUEUES_PER_PORT];
    queue[0] = 0;
    uint8_t rss_key[40];
    struct rte_eth_rss_conf rss_conf;
    rss_conf.rss_key = rss_key;
    rss_conf.rss_key_len = 40;
    rte_eth_dev_rss_hash_conf_get(port_id, &rss_conf);
    rss.types = rss_conf.rss_hf;
    rss.key_len = rss_conf.rss_key_len;
    rss.queue_num = 1;
    rss.key = rss_key;
    rss.queue = queue;
    rss.level = 0;
    rss.func = RTE_ETH_HASH_FUNCTION_DEFAULT;
    action[1].conf = &rss;*/

    action[2].type = RTE_FLOW_ACTION_TYPE_END;
    }


    rte_flow_item end;
    memset(&end, 0, sizeof(struct rte_flow_item));
    end.type =  RTE_FLOW_ITEM_TYPE_END;
    pattern.push_back(end);

    struct rte_flow_error error;
    int res;
    res = rte_flow_validate(port_id, &attr, pattern.data(), action, &error);
    if (!res) {
        struct rte_flow *flow = rte_flow_create(port_id, &attr, pattern.data(), action, &error);
        if (flow) {
            click_chatter("Flow added succesfully with %d patterns to id %d with action %s !", pattern.size(), _matches.size(), action[0].type == RTE_FLOW_ACTION_TYPE_DROP? "drop":"rss");
        } else {

            click_chatter("Could not add pattern with %d patterns, error %d : %s", pattern.size(), res, error.message);
        }
    } else {
        click_chatter("Could not validate pattern with %d patterns, error %d : %s", pattern.size(),  res, error.message);
    }

}

int FlowDPDKClassifier::traverse_rules(FlowNode* node, Vector<rte_flow_item> &pattern, rte_flow_item_type last_layer, int offset) {
    if (node->level()->is_dynamic()) {
        click_chatter("Node is dynamic, adding this node and stopping");
       add_rule(pattern, FlowNodePtr(node));
       return 0;
    }

    FlowNode::NodeIterator it = node->iterator();

    FlowNodePtr* cur;

    rte_flow_item pat;
    bzero(&pat,sizeof(rte_flow_item));
    pattern.push_back(pat);
        rte_flow_item_type new_layer;
        int new_offset;

    while ((cur = it.next()) != 0) {
        int ret = node->level()->to_dpdk_flow(cur->data(), last_layer, offset, new_layer, new_offset, pattern[pattern.size() - 1], false);
        if (ret != 0) {
            goto addthis;
        } else {
            click_chatter("Pattern found");
        }

        if (cur->is_leaf()) {
            click_chatter("Adding a leaf");
            add_rule(pattern, FlowNodePtr(cur->leaf));
        } else {
            traverse_rules(cur->node, pattern, new_layer, new_offset);
        }
    }

    if (node->default_ptr()->ptr != 0) {
        int ret = node->level()->to_dpdk_flow(node->default_ptr()->data(), last_layer, offset, new_layer, new_offset, pattern[pattern.size() - 1], true);
        if (ret != 0) {
            goto addthis;
        }
        if (node->default_ptr()->is_leaf()) {
            click_chatter("Adding default leaf");
            add_rule(pattern, FlowNodePtr(node->default_ptr()->leaf));
        } else {
             traverse_rules(node->default_ptr()->node, pattern, new_layer, new_offset);
        }
    }

    pattern.pop_back();
    return 0;
addthis:
    click_chatter("Unknown classification %s, stopping", node->level()->print().c_str());
    pattern.pop_back();
    add_rule(pattern, FlowNodePtr(node));
    return 0;

}

int FlowDPDKClassifier::initialize(ErrorHandler *errh) {
    if (_initialize_classifier(errh) != 0)
        return -1;
    if (_replace_leafs(errh) != 0)
        return -1;

    Vector<rte_flow_item> pattern;
    traverse_rules(_table.get_root(), pattern, RTE_FLOW_ITEM_TYPE_RAW, 0);

    if (_initialize_timers(errh) != 0)
        return -1;
}

void FlowDPDKClassifier::push_batch(int port, PacketBatch* batch) {
    FlowControlBlock* tmp_stack = fcb_stack;
    FlowTableHolder* tmp_table = fcb_table;

    fcb_table = &_table;
    Packet* last = 0;
    int count = 0;
    PacketBatch* awaiting_batch = 0;
    Timestamp now = Timestamp::recent_steady();
    Packet* p = batch;
    while (p != NULL) {

        Packet* next = p->next();
        FlowNodePtr ptr;
        if (AGGREGATE_ANNO(p)) {
            ptr = _matches[AGGREGATE_ANNO(p) - 1];
//            click_chatter("Id %d, ptr %p", AGGREGATE_ANNO(p), ptr.ptr );
            if (ptr.is_node()) {
                ptr.leaf = _table.match(p, ptr.node);
            }
        } else
            ptr.leaf = _table.match(p);

        if (!is_valid_fcb(p, last, next, ptr.leaf, now))
            continue;

        handle_simple(p, last, ptr.leaf, awaiting_batch, count, now);

        last = p;
        p = next;
    }

    flush_simple(last, awaiting_batch, count,  now);

    fcb_stack = tmp_stack;
    fcb_table = tmp_table;
}



CLICK_ENDDECLS

EXPORT_ELEMENT(FlowDPDKClassifier)
ELEMENT_REQUIRES(FlowClassifier dpdk)
