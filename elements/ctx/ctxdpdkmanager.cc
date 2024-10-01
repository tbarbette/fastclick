#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include "ctxdpdkmanager.hh"

#include <click/flow/flow.hh>
#include <click/flow/node/flow_nodes.hh>

#if RTE_VERSION >= RTE_VERSION_NUM(22,07,0,0)
#define PKT_RX_FDIR_ID RTE_MBUF_F_RX_FDIR_ID
#endif

CLICK_DECLS


CTXDPDKManager::CTXDPDKManager() {
    _builder = false;
}

CTXDPDKManager::~CTXDPDKManager() {

}

int CTXDPDKManager::configure(Vector<String> &conf, ErrorHandler *errh) {

    Element* dev;
    if (Args(this, errh).bind(conf)
            .read_mp("DEVICE", dev)
            //.validate_p("AGGCACHE", _aggcache, false)
            .consume() < 0)
        return -1;

    if (CTXManager::configure(conf, errh) != 0)
        return -1;

    _dev = dynamic_cast<FromDPDKDevice*>(dev);

    return 0;
}

void *
CTXDPDKManager::cast(const char *n)
{
    if (strcmp(n, "CTXManager") == 0)
	return dynamic_cast<CTXManager *>(this);
    else
	return Element::cast(n);
}



void CTXDPDKManager::add_rule(Vector<rte_flow_item> pattern, FlowNodePtr ptr) {
    if (pattern.size() == 0) {
        click_chatter("Cannot add classifier without rules !");
        return;
    }
    _matches.push_back(ptr);

    int port_id = _dev->get_device()->get_port_id();

    bool is_ip = false;
    for (int i = 0; i < pattern.size(); i++) {
        if (pattern[i].type == RTE_FLOW_ITEM_TYPE_TCP || pattern[i].type == RTE_FLOW_ITEM_TYPE_UDP)
            is_ip = true;
    }

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

        if (!is_ip) {
            struct rte_flow_action_queue queue;
            action[1].type = RTE_FLOW_ACTION_TYPE_QUEUE;
            queue.index = 0;
            action[1].conf = &queue;
        } else {
            action[1].type = RTE_FLOW_ACTION_TYPE_RSS;
            uint16_t queue[RTE_MAX_QUEUES_PER_PORT];
            auto threads = get_passing_threads();
            int id = 0;
            for (int i = 0; i < threads.size(); i++) {
                if (!threads[i])
                    continue;
                queue[id++] = id;
            }
            uint8_t rss_key[40];
            struct rte_eth_rss_conf rss_conf;
            rss_conf.rss_key = rss_key;
            rss_conf.rss_key_len = 40;
            rte_eth_dev_rss_hash_conf_get(port_id, &rss_conf);
            rss.types = rss_conf.rss_hf;
            rss.key_len = rss_conf.rss_key_len;
            rss.queue_num = id;
            rss.key = rss_key;
            rss.queue = queue;
            rss.level = 0;
            rss.func = RTE_ETH_HASH_FUNCTION_DEFAULT;
            action[1].conf = &rss;
        }
        action[2].type = RTE_FLOW_ACTION_TYPE_END;
    }


    rte_flow_item end;
    memset(&end, 0, sizeof(struct rte_flow_item));
    end.type =  RTE_FLOW_ITEM_TYPE_END;
    pattern.push_back(end);

    struct rte_flow_error error;
    int res;
    res = rte_flow_validate(port_id, &attr, pattern.data(), action, &error);
    const char* actiont = action[0].type == RTE_FLOW_ACTION_TYPE_DROP? "drop":(is_ip?"rss":"queue");
    if (!res) {
        struct rte_flow *flow = rte_flow_create(port_id, &attr, pattern.data(), action, &error);
        if (flow) {
            click_chatter("Flow added succesfully with %d patterns to id %d with action %s !", pattern.size(), _matches.size(), actiont);
        } else {

            click_chatter("Could not add pattern with %d patterns with action %s, error %d : %s", pattern.size(), actiont, res, error.message);
        }
    } else {
        click_chatter("Could not validate pattern with %d patterns with action %s, error %d : %s", pattern.size(), actiont,  res, error.message);
    }

}

int CTXDPDKManager::traverse_rules(FlowNode* node, Vector<rte_flow_item> pattern, rte_flow_item_type last_layer, int offset) {
    if (node->level()->is_dynamic()) {
        click_chatter("Node is dynamic, adding this node and stopping");
       add_rule(pattern, FlowNodePtr(node));

       return 0;
    }

    FlowNode::NodeIterator it = node->iterator();

    FlowNodePtr* cur;

    rte_flow_item pat;
    bzero(&pat,sizeof(rte_flow_item));
    rte_flow_item_type new_layer;
    int new_offset;

    while ((cur = it.next()) != 0) {
        int ret = node->level()->to_dpdk_flow(cur->data(), last_layer, offset, new_layer, new_offset, pattern, false);
        if (ret < 0) {
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
        for (int i = 0; i < ret; i++)
            pattern.pop_back();
    }

    if (node->default_ptr()->ptr != 0) {
        int ret = node->level()->to_dpdk_flow(node->default_ptr()->data(), last_layer, offset, new_layer, new_offset, pattern, true);
        if (ret < 0) {
            goto addthis;
        }
        if (node->default_ptr()->is_leaf()) {
            click_chatter("Adding default leaf");
            add_rule(pattern, FlowNodePtr(node->default_ptr()->leaf));
        } else {
             traverse_rules(node->default_ptr()->node, pattern, new_layer, new_offset);
        }
        for (int i = 0; i < ret; i++)
            pattern.pop_back();
    }

    return 0;
addthis:
    click_chatter("Unknown classification %s, stopping", node->level()->print().c_str());
    add_rule(pattern, FlowNodePtr(node));
    return 0;

}

int CTXDPDKManager::initialize(ErrorHandler *errh) {
    if (_initialize_classifier(errh) != 0)
        return -1;
    if (_replace_leafs(errh) != 0)
        return -1;

    if (!_builder && cast("CTXDPDKBuilderManager"))
        return errh->error("This class is hardcoded to use builder. Use CTXDPDKManager or a variant to disable builder.");
    if (_builder && !cast("CTXDPDKBuilderManager"))
        return errh->error("This class is hardcoded to not use builder. Use CTXDPDKBuilderManager");
    if (_aggcache && !cast("CTXDPDKCacheManager"))
        return errh->error("This class is hardcoded to not use cache. Use CTXDPDKCacheManager");
    if (!_aggcache && cast("CTXDPDKCacheManager"))
        return errh->error("This class is hardcoded to use cache. Use CTXDPDKManager or CTXDPDKBuilderManager to disable cache");

    if (_aggcache && _cache_size == 0)
        return errh->error("With a cache size of 0, AGG is actualy disabled...");

    Vector<rte_flow_item> pattern;
    traverse_rules(_table.get_root(), pattern, RTE_FLOW_ITEM_TYPE_RAW, 0);

    if (_initialize_timers(errh) != 0)
        return -1;
    return 0;
}

void CTXDPDKManager::push_batch(int port, PacketBatch* batch) {
    FlowControlBlock* tmp_stack = fcb_stack;
    FlowTableHolder* tmp_table = fcb_table;

    fcb_table = &_table;
    Packet* last = 0;
    int count = 0;
    PacketBatch* awaiting_batch = 0;
    Timestamp now = Timestamp::recent_steady();
    Packet* p = batch->first();
    while (p != NULL) {

        Packet* next = p->next();
        FlowNodePtr ptr;
        rte_mbuf* mbuf = (rte_mbuf*)p->destructor_argument();
        if (likely(mbuf->ol_flags & PKT_RX_FDIR_ID)) {
            int fid = mbuf->hash.fdir.hi;
            ptr = _matches[fid - 1];
            if (ptr.is_node()) {
                ptr.leaf = _table.match(p, ptr.node);
            }
        } else {
            ptr.leaf = _table.match(p);
        }


        if (!is_valid_fcb(p, last, next, ptr.leaf, now))
            continue;

        handle_simple(p, last, ptr.leaf, awaiting_batch, count, now);

        last = p;
        p = next;
    }

    flush_simple(last, awaiting_batch, count,  now);
    check_release_flows();

    fcb_stack = tmp_stack;
    fcb_table = tmp_table;
}

CTXDPDKCacheManager::CTXDPDKCacheManager() {
    _builder = false;
    _aggcache = true;
}

CTXDPDKCacheManager::~CTXDPDKCacheManager() {

}


void CTXDPDKCacheManager::push_batch(int port, PacketBatch* batch) {
    FlowControlBlock* tmp_stack = fcb_stack;
    FlowTableHolder* tmp_table = fcb_table;

    fcb_table = &_table;
    Packet* last = 0;
    int count = 0;
    uint32_t lastagg = 0;
    PacketBatch* awaiting_batch = 0;
    Timestamp now = Timestamp::recent_steady();
    Packet* p = batch->first();
    FlowControlBlock* fcb = 0;
    while (p != NULL) {

        Packet* next = p->next();
        rte_mbuf* mbuf = (rte_mbuf*)p->destructor_argument();
        if (likely(mbuf->ol_flags & PKT_RX_FDIR_ID)) {
            FlowNodePtr ptr;
            int fid = mbuf->hash.fdir.hi;
            ptr = _matches[fid - 1];
            if (ptr.is_node()) {
                uint32_t agg = AGGREGATE_ANNO(p);
                if (!(lastagg == agg && fcb && likely(fcb->parent && _table.reverse_match(fcb,p,ptr.node)))) {
                    fcb = get_cache_fcb(p,agg,ptr.node);
                    lastagg = agg;
                }  //else fcb is still a valid pointer
            } else {
                fcb = ptr.leaf;
            }
        } else {
            uint32_t agg = AGGREGATE_ANNO(p);
            if (!(lastagg == agg && fcb && likely(fcb->parent && _table.reverse_match(fcb,p,_table.get_root())))) {
                fcb = get_cache_fcb(p,agg,_table.get_root());
                lastagg = agg;
            } //else fcb is still a valid pointer
        }

        if (!is_valid_fcb(p, last, next, fcb, now))
            continue;

        handle_simple(p, last, fcb, awaiting_batch, count, now);

        last = p;
        p = next;
    }

    flush_simple(last, awaiting_batch, count,  now);
    check_release_flows();

    fcb_stack = tmp_stack;
    fcb_table = tmp_table;
}

CTXDPDKBuilderManager::CTXDPDKBuilderManager() {
    _builder = true;
    _aggcache = false;
}

CTXDPDKBuilderManager::~CTXDPDKBuilderManager() {

}



void CTXDPDKBuilderManager::push_batch(int port, PacketBatch* batch) {
    FlowControlBlock* tmp_stack = fcb_stack;
    FlowTableHolder* tmp_table = fcb_table;

    fcb_table = &_table;
    Packet* last = 0;
    Timestamp now = Timestamp::recent_steady();

    Builder builder;

    Packet* p = batch->first();
    while (p != NULL) {

        Packet* next = p->next();
        FlowNodePtr ptr;

        rte_mbuf* mbuf = (rte_mbuf*)p->destructor_argument();
        if (likely(mbuf->ol_flags & PKT_RX_FDIR_ID)) {
            int fid = mbuf->hash.fdir.hi;
            ptr = _matches[fid - 1];
            if (ptr.is_node()) {
                ptr.leaf = _table.match(p, ptr.node);
            }
        } else
            ptr.leaf = _table.match(p);

        if (!is_valid_fcb(p, last, next, ptr.leaf, now))
            continue;

        handle_builder(p, last, ptr.leaf, builder, now);

        last = p;
        p = next;
    }

    flush_builder(last, builder,  now);
    check_release_flows();

    fcb_stack = tmp_stack;
    fcb_table = tmp_table;
}


CLICK_ENDDECLS

EXPORT_ELEMENT(CTXDPDKManager)
EXPORT_ELEMENT(CTXDPDKBuilderManager)
EXPORT_ELEMENT(CTXDPDKCacheManager)
ELEMENT_REQUIRES(CTXManager dpdk)
