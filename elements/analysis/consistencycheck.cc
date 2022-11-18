/*
 * consistencycheck.{cc,hh} -- check flow consistency
 * Massimo Girondi
 *
 * Copyright (c) 2021 KTH Royal Institute of Technology
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
#include "consistencycheck.hh"
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>
CLICK_DECLS

ConsistencyCheck::ConsistencyCheck(): _verbose(0), _table_size(65535)
{
}


int
ConsistencyCheck::configure(Vector<String> &conf, ErrorHandler* errh)
{

    if (Args(conf, this, errh)
	.read_or_set_p("OFFSET", _offset, 40)
	.read("VERBOSE", _verbose)
	.read_or_set("CAPACITY", _table_size, 65536)
	.complete() < 0)
	return -1;

  return 0;
}

int ConsistencyCheck::initialize(ErrorHandler *errh)
{
    auto passing = get_passing_threads();
    _table_size = next_pow2(_table_size/passing.weight());
    click_chatter("%s: Real capacity for each table will be %d", class_name(), _table_size);

    _tables = CLICK_ALIGNED_NEW(pcctable, passing.size());
    CLICK_ASSERT_ALIGNED(_tables);
    _threads = passing.size();

    memset(_tables,0, sizeof(pcctable) * passing.size());

    for (int i = 0; i < _threads; i++) {
        if (passing[i])
	{
	    _tables[i].hash = new HashTableH<IPFlow5ID, int>();
	    _tables[i].hash->resize_clear(_table_size);
	    _tables[i].current = 0;
	    _tables[i].too_short = 0;

	    if (!_tables[i].hash)
		return errh->error("Could not init pcc table %d!", i);

	    click_chatter("%s::alloc %i", class_name(), i);
	    _tables[i].flowids =  (uint64_t*)CLICK_ALIGNED_ALLOC(sizeof(uint64_t) * _table_size);
	    _tables[i].counts =  (uint32_t*)CLICK_ALIGNED_ALLOC(sizeof(uint32_t) * _table_size);
	    _tables[i].broken =  (uint32_t*)CLICK_ALIGNED_ALLOC(sizeof(uint32_t) * _table_size);
	    CLICK_ASSERT_ALIGNED(_tables[i].flowids);
	    CLICK_ASSERT_ALIGNED(_tables[i].counts);
	    CLICK_ASSERT_ALIGNED(_tables[i].broken);
	    memset(_tables[i].flowids, 0, sizeof(uint64_t) * _table_size);
	    memset(_tables[i].counts, 0, sizeof(uint32_t) * _table_size);
	    memset(_tables[i].broken, 0, sizeof(uint32_t) * _table_size);
	    if (!_tables[i].flowids || !_tables[i].counts || !_tables[i].broken)
		return errh->error("Could not init data table %d!", i);
	}
    
    }
    
    return 0;

}

inline Packet *
ConsistencyCheck::process(Packet* p) {

    auto& tab = _tables[click_current_cpu_id()];
    if(p->length() >= (_offset + 8))
    {

	const uint64_t stored = * reinterpret_cast<const uint64_t*>(p->data()+_offset);
	// click_chatter("STORED %08X -> %lu", stored, stored);

	IPFlow5ID fid = IPFlow5ID(p);
	//uint32_t hashed = ipv4_hash_crc(&fid, sizeof(uint32_t), 0);
	uint32_t hashed = (uint32_t) fid.hashcode();

	auto * table = tab.hash;

	auto ret = table->find_create(fid, [&tab, stored, hashed, &p, this](){
		int id = tab.current.fetch_and_add(1);
		tab.flowids[id] = stored;
		tab.broken[id] = 0;
		tab.counts[id] = 0;
		if(unlikely(_verbose))
		{
		    // char buff[1000];
		    // buff[0]=0;
		    // for(int i=0; i<p->length() && i<120; i+=1)
			// sprintf( buff, "%s%02X", buff, (p->data()[i]));
		    //sprintf(buff,"%s\0", buff);

		    click_chatter("[%08X] NEW Flow id %lu.", // PKT %s",
			hashed, tab.flowids[id]); //, buff);
		}
		return id;});

	tab.counts[*ret]++;
	if(unlikely(_verbose))
	    click_chatter("[%08X] OLD Flow id %lu, count %i, broken %i",
		    hashed, tab.flowids[*ret], tab.counts[*ret], tab.broken[*ret]);

	if(unlikely(tab.flowids[*ret] != stored))
	{

	    tab.broken[*ret]++;
	    if(unlikely(_verbose))
	    {
		// char buff[1000];
		// buff[0]=0;
		// for(int i=0; i<p->length() && i<120; i+=1)
		//     sprintf( buff, "%s%02X", buff, (p->data()[i]));
		// sprintf(buff,"%s\0", buff);

		click_chatter("[%08X] Broken connection: this flowid is %lu, last time was %i. Already broken %i/%i times.", // PKT %s",
			hashed, stored,  tab.flowids[*ret], tab.broken[*ret], tab.counts[*ret]); //, buff);
	    }
	    tab.flowids[*ret] = stored;
	}
    }
    else
    {
	if(unlikely(_verbose))
	    click_chatter("Packet too short, lenghth is %i", p->length());
	tab.too_short++;
    }

    return p;

}

#if HAVE_BATCH
void
ConsistencyCheck::push_batch(int, PacketBatch* batch)
{
	EXECUTE_FOR_EACH_PACKET(process, batch);
	output_push_batch(0, batch);
}
#endif

enum {h_broken_conn, h_broken_pkts, h_broken_conn_ratio, h_broken_pkts_ratio, h_too_short, h_total_connections, h_avg_broken};
String ConsistencyCheck::read_handler(Element* e, void* thunk)
{
    ConsistencyCheck* cc = static_cast<ConsistencyCheck*>(e);
    switch ((intptr_t)thunk) {
    case h_broken_conn:
	{
	    uint64_t total = 0;
	    for(int i=0; i<cc->_threads; i++)
		if (cc->_tables[i].hash != 0)
		    for(int j=0; j<cc->_table_size; j++)
			total+= cc->_tables[i].broken[j] >0;
	    return String(total);
	}
    case h_broken_pkts:
	{
	    uint64_t total = 0;
	    for(int i=0; i<cc->_threads; i++)
		if (cc->_tables[i].hash != 0)
		    for(int j=0; j<cc->_table_size; j++)
			total+= (cc->_tables[i].broken)[j];
	    return String(total);
	}
    case h_too_short:
	{
	    uint64_t total = 0;
	    for(int i=0; i<cc->_threads; i++)
		if (cc->_tables[i].hash != 0)
		    total+= cc->_tables[i].too_short;
	    return String(total);
	}
    case h_total_connections:
	{
	    uint64_t total = 0;
	    for(int i=0; i<cc->_threads; i++)
		if (cc->_tables[i].hash != 0)
		    total+= cc->_tables[i].hash->size();
	    return String(total);
	}

    case h_broken_conn_ratio:
	{
	    uint64_t total = 0;
	    uint64_t broken = 0;
	    for(int i=0; i<cc->_threads; i++)
	    {
		if (cc->_tables[i].hash != 0)
		{
		    for(int j=0; j<cc->_table_size; j++)
			broken+= ((cc->_tables[i].broken)[j] > 0);
		    total+=cc->_tables[i].hash->size();
		}
	    }
	    return String(total != 0 ? broken / (float) total : 0);
	}

    case h_broken_pkts_ratio:
	{
	    uint64_t total = 0;
	    uint64_t broken = 0;
	    for(int i=0; i<cc->_threads; i++)
	    {
		if (cc->_tables[i].hash != 0)
		{
		    for(int j=0; j<cc->_table_size; j++)
		    {
			broken+= ((cc->_tables[i].broken)[j] > 0);
			total+=cc->_tables[i].counts[j];
		    }
		}
	    }
	    return String( total != 0 ? broken / (float) total : 0);
	}
    case h_avg_broken:
	{
	    uint32_t total = 0;
	    double sum = 0;
	    for(int i=0; i<cc->_threads; i++)
	    {
		if (cc->_tables[i].hash != 0)
		{
		    for(int j=0; j<cc->_table_size; j++)
		    {
			if(cc->_tables[i].broken[j] >0)
			{
			    sum += (cc->_tables[i].broken)[j];
			    total++;
			}
		    }
		}
	    }
	    return String( total != 0 ? sum / total : 0);
	}

    default:
        return "<error>";
    }
};

void ConsistencyCheck::add_handlers()
{
    add_read_handler("broken_connections", read_handler, h_broken_conn);
    add_read_handler("broken_packets", read_handler, h_broken_pkts);
    add_read_handler("broken_packets_ratio", read_handler, h_broken_pkts_ratio);
    add_read_handler("broken_connections_ratio", read_handler, h_broken_conn_ratio);
    add_read_handler("too_short", read_handler, h_too_short);
    add_read_handler("total_connections", read_handler, h_total_connections);
    add_read_handler("avg_broken_per_connection", read_handler, h_avg_broken);

}

CLICK_ENDDECLS

ELEMENT_REQUIRES(dpdk)
EXPORT_ELEMENT(ConsistencyCheck)
ELEMENT_MT_SAFE(ConsistencyCheck)
