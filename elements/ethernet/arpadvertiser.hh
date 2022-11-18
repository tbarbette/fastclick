#ifndef CLICK_ARPADVERTISER_HH
#define CLICK_ARPADVERTISER_HH

#include <click/timer.hh>
#include <click/etheraddress.hh>
#include <click/ipaddress.hh>
#include <click/vector.hh>
#include <click/batchelement.hh>
#include <click/straccum.hh>

CLICK_DECLS

/*
=c
ARPAdvertiser([DSTIP, DSTETH, SRCIP, SRCETH, CAPACITY])

=s arp
periodically generates ARP replies using information from an ARP table

=d
For each entry in its ARP table, it periodically sends an ARP "reply"
packet to DSTIP/DSTETH, claiming that SRCIP has ethernet address SRCETH.
Generates the Ethernet header as well as the ARP header.
The ARP table can be populated/evacuated through insert/delete/clean handlers.

=e
Sends ARP packets to 18.26.4.1 (with ether addr 00-E0-2B-0B-1A-00)
claiming that 18.26.4.99's ethernet address is 00-A0-C9-9C-FD-9C.

  ARPAdvertiser(DSTIP 18.26.4.1, DSTETH 0:e0:2b:b:1a:0, SRCIP 18.26.4.99, SRCETH 00:a0:c9:9c:fd:9c)
     -> ToDevice(eth0);

=n
ARPAdvertiser is useful for ARP proxies.
Normally, you should use ARPResponder rather than ARPAdvertiser.

=over 8

=h table read-only

Returns the current ARP table.

=h count read-only

Returns the current number of ARP table entries.

=h capacity read-only

Returns the current capacity of the ARP table.

=h insert write-only

Inserts a new entry in the ARP table.

=h delete write-only

Deletes an entry from the ARP table.

=back

=a
ARPFaker, ARPQuerier, ARPResponder
*/

class ARPAdvertiser : public BatchElement {

    public:
        ARPAdvertiser() CLICK_COLD;
        ~ARPAdvertiser() CLICK_COLD;

        const char *class_name() const override    { return "ARPAdvertiser"; }
        const char *port_count() const override    { return PORTS_0_1; }
        const char *processing() const override    { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void add_handlers() CLICK_COLD;

        void run_timer(Timer *);

        static unsigned DEFAULT_ARP_TABLE_CAPACITY;
        static uint32_t DEFAULT_ARP_ADV_PERIOD_MS;

    private:
        class ARPAdvertismentTuple {
            public:
                IPAddress dst_ip;
                EtherAddress dst_mac;
                IPAddress adv_ip;
                EtherAddress adv_mac;

                ARPAdvertismentTuple() {}

                ARPAdvertismentTuple(
                    IPAddress d_ip, EtherAddress d_mac,
                    IPAddress a_ip, EtherAddress a_mac
                ) : dst_ip(d_ip), dst_mac(d_mac), adv_ip(a_ip), adv_mac(a_mac) {}

                bool empty() {
                    EtherAddress zero_mac;

                    if ((this->dst_ip.empty()) ||
                        (this->dst_mac.unparse() == zero_mac.unparse()) ||
                        (this->adv_ip.empty()) ||
                        (this->adv_mac.unparse() == zero_mac.unparse())) {
                        return true;
                    }

                    return false;
                }

                bool operator==(const ARPAdvertismentTuple &other) {
                    if ((this->dst_ip  != other.dst_ip) ||
                        (this->dst_mac != other.dst_mac) ||
                        (this->adv_ip  != other.adv_ip) ||
                        (this->adv_mac != other.adv_mac)) {
                        return false;
                    }

                    return true;
                }

                String to_str_raw() {
                    StringAccum sa;

                    sa << this->dst_ip.unparse().c_str() << ", ";
                    sa << this->dst_mac.unparse_colon().c_str() << ", ";
                    sa << this->adv_ip.unparse().c_str() << ", ";
                    sa << this->adv_mac.unparse_colon().c_str() << "\n";

                    return sa.take_string();
                }

                String to_str() {
                    StringAccum sa;
                    short tab_size = 17;

                    sa << "[Dst IP ";
                    sa.snprintf(tab_size, "%17s", this->dst_ip.unparse().c_str());
                    sa << ", Dst MAC ";
                    sa.snprintf(tab_size, "%17s", this->dst_mac.unparse_colon().c_str());
                    sa << "] --> [Adv IP ";
                    sa.snprintf(tab_size, "%17s", this->adv_ip.unparse().c_str());
                    sa << ", Adv MAC ";
                    sa.snprintf(tab_size, "%17s", this->adv_mac.unparse_colon().c_str());
                    sa << "]\n";

                    return sa.take_string();
                }
        };

        Timer _timer;
        Vector<ARPAdvertismentTuple> _arp_table;
        Spinlock *_lock;

        enum { h_table, h_count, h_capacity, h_insert, h_delete, h_clear };

        void reschedule(const uint32_t &period_msec);
        bool arp_table_contains(ARPAdvertismentTuple &tuple);

        static String read_handler(Element *, void *) CLICK_COLD;
        static int write_handler(const String &, Element *, void *, ErrorHandler *) CLICK_COLD;
};

CLICK_ENDDECLS
#endif
