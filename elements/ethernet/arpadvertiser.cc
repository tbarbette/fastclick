/*
 * arpadvertiser.{cc,hh} -- ARP advertiser element
 * Georgios Katsikas
 *
 * Copyright (c) 2020 UBITECH and KTH Royal Institute of Technology
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
#include <click/args.hh>
#include <click/error.hh>
#include <click/sync.hh>

#include "arpadvertiser.hh"
#include "arpresponder.hh"

CLICK_DECLS

// Default capacity of the ARP table
unsigned ARPAdvertiser::DEFAULT_ARP_TABLE_CAPACITY = 10;

// Default period for sending ARP advertisements (in ms)
uint32_t ARPAdvertiser::DEFAULT_ARP_ADV_PERIOD_MS = 10000;

ARPAdvertiser::ARPAdvertiser() : _timer(this), _arp_table(), _lock(0)
{
#if HAVE_BATCH
    in_batch_mode = BATCH_MODE_YES;
#endif
}

ARPAdvertiser::~ARPAdvertiser()
{
    _arp_table.clear();
    if (_lock) {
        delete _lock;
        _lock = 0;
    }
}

int
ARPAdvertiser::configure(Vector<String> &conf, ErrorHandler *errh)
{
    unsigned capacity = DEFAULT_ARP_TABLE_CAPACITY;
    ARPAdvertiser::ARPAdvertismentTuple input_arp_entry;

    if (Args(conf, this, errh)
        .read_p("DSTIP", input_arp_entry.dst_ip)
        .read_p("DSTETH", input_arp_entry.dst_mac)
        .read_p("SRCIP", input_arp_entry.adv_ip)
        .read_p("SRCETH", input_arp_entry.adv_mac)
        .read("CAPACITY", capacity)
        .complete() < 0)
        return -1;

    if (capacity <= 0) {
        return errh->error("ARP table capacity must be a positive integer");
    }
    _arp_table.reserve(capacity);

    if (!input_arp_entry.empty()) {
        _arp_table.push_back(input_arp_entry);
    }

    return 0;
}

int
ARPAdvertiser::initialize(ErrorHandler *errh)
{
    _timer.initialize(this);

    _lock = new Spinlock();
    if (!_lock) {
        return errh->error("Cannot create spinlock");
    }

    // Quick re-scheduling
    reschedule(1000);

    return 0;
}

void
ARPAdvertiser::reschedule(const uint32_t &period_msec)
{
    _timer.schedule_after_msec(period_msec);
}

void
ARPAdvertiser::run_timer(Timer *)
{
#if HAVE_BATCH
    PacketBatch *head = 0;
    Packet *last;
#endif

    _lock->acquire();
    unsigned current_size = _arp_table.size();

    for (auto entry : _arp_table) {
        Packet *p = ARPResponder::make_response(
            entry.dst_mac.data(), entry.dst_ip.data(),
            entry.adv_mac.data(), entry.adv_ip.data()
        );
        if (p) {
        #if HAVE_BATCH
            if (!head)
                head = PacketBatch::start_head(p);
            else
                last->set_next(p);
            last = p;
        #else
            output(0).push(p);
        #endif
        }
    }

    _lock->release();

#if HAVE_BATCH
    if (head) {
        head->make_tail(last, current_size);
        output_push_batch(0, head);
    }
#endif

    // Re-schedule according to a fixed period
    reschedule(DEFAULT_ARP_ADV_PERIOD_MS);
}

bool
ARPAdvertiser::arp_table_contains(ARPAdvertismentTuple &tuple)
{
    for (auto entry : _arp_table) {
        if (tuple == entry) {
            return true;
        }
    }

    return false;
}

String
ARPAdvertiser::read_handler(Element *e, void *thunk)
{
    ARPAdvertiser *f = (ARPAdvertiser *) e;

    switch (reinterpret_cast<uintptr_t>(thunk)) {
        case h_table: {
            f->_lock->acquire();

            StringAccum sa;
            for (auto entry : f->_arp_table) {
                sa << entry.to_str().c_str();
            }

            f->_lock->release();

            return String(sa.take_string());
        }
        case h_count:
            return String(f->_arp_table.size());
        case h_capacity:
            return String(f->_arp_table.capacity());
        default:
            return String();
    }
}

int
ARPAdvertiser::write_handler(const String &str, Element *e, void *thunk, ErrorHandler *errh)
{
    ARPAdvertiser *f = (ARPAdvertiser *) e;

    switch (reinterpret_cast<uintptr_t>(thunk)) {
        case h_insert:
        case h_delete: {
            ARPAdvertiser::ARPAdvertismentTuple arp_entry;
            Vector<String> tokens = str.split(' ');
            if (Args(tokens, f, errh)
                .read_mp("DSTIP", arp_entry.dst_ip)
                .read_mp("DSTETH", arp_entry.dst_mac)
                .read_mp("SRCIP", arp_entry.adv_ip)
                .read_mp("SRCETH", arp_entry.adv_mac)
                .complete() < 0) {
                return errh->error("ARP table requires 4 space-separated arguments: DSTIP DSTETH SRCIP SRCETH");
            }

            f->_lock->acquire();

            unsigned init_count = f->_arp_table.size();
            unsigned init_capacity = f->_arp_table.capacity();
            int status = -1;

            // Insertion
            if ((uintptr_t) thunk == h_insert) {
                // Enlarge the ARP table if needed
                if (init_capacity < (init_count + 1)) {
                    f->_arp_table.resize(2 * init_capacity);
                }

                // Insert unique
                if (f->arp_table_contains(arp_entry)) {
                    errh->warning("Already present ARP entry: %s", str.c_str());
                } else {
                    f->_arp_table.push_back(arp_entry);
                    errh->message("Successfully inserted ARP entry: %s", str.c_str());
                    status = 0;
                }
            // Deletion
            } else {
                for (Vector<ARPAdvertismentTuple>::iterator it = f->_arp_table.begin();
                        it != f->_arp_table.end(); ++it) {
                    if (*it == arp_entry) {
                        f->_arp_table.erase(it);
                        errh->message("Successfully deleted ARP entry: %s", str.c_str());
                        status = 0;
                        break;
                    }
                }

                if (status != 0) {
                    errh->warning("Cannot find ARP entry: %s", str.c_str());
                }
            }

            f->_lock->release();

            return status;
        }
        case h_clear: {
            f->_lock->acquire();

            f->_arp_table.clear();
            assert(f->_arp_table.size() == 0);

            f->_lock->release();

            errh->message("Successfully flushed ARP table");
            return 0;
        }
        default:
            return -1;
    }
}

void
ARPAdvertiser::add_handlers()
{
    add_read_handler("table",    read_handler,  h_table);
    add_read_handler("count",    read_handler,  h_count);
    add_read_handler("capacity", read_handler,  h_capacity);
    add_write_handler("insert",  write_handler, h_insert);
    add_write_handler("delete",  write_handler, h_delete);
    add_write_handler("clear",   write_handler, h_clear);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(ARPResponder)
EXPORT_ELEMENT(ARPAdvertiser)
