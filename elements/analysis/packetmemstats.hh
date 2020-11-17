// -*- c-basic-offset: 4 -*-
#ifndef CLICK_PACKETMEMSTATS_HH
#define CLICK_PACKETMEMSTATS_HH

#include <click/batchelement.hh>
#include <click/multithread.hh>

#define DEF_ALIGN 64

CLICK_DECLS

/*

=c

PacketMemStats([ALIGN_STRIDE])

=s counters

keep statistics about packet memory

=d

Expects Ethernet packets as input. Checks whether input packets are aligned
with respect to the ALIGN_STRIDE argument and counts aligned and total packets.
Reports the ratio of aligned/total and unaligned/total packets.

Keyword arguments are:

=over 1

=item ALIGN_STRIDE

Unsigned integer. Defines the stride of the alignment. Defaults to 64 (usual cache line length).

=e

  FromDevice(eth0) -> PacketMemStats() -> ...

=h align_stride read-only

Returns the stride of the alignment.

=h aligned_pkts read-only

Returns the number of aligned packets with respect to the stride.

=h unaligned_pkts read-only

Returns the number of unaligned packets with respect to the stride.

=h total_pkts read-only

Returns the total number of packets seen so far.

=h align_stride write-only

Updates the alignment stride.

=a BatchStats */

class PacketMemStats : public BatchElement {

    public:
        PacketMemStats() CLICK_COLD;
        PacketMemStats(unsigned align) CLICK_COLD;
        ~PacketMemStats() CLICK_COLD;

        const char *class_name() const override  { return "PacketMemStats"; }
        const char *port_count() const override  { return PORTS_1_1; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;

        Packet *simple_action(Packet *) override;
    #if HAVE_BATCH
        PacketBatch *simple_action_batch(PacketBatch *) override;
    #endif

        inline unsigned get_align() {
            return _align;
        }

        inline void set_align(unsigned align) {
            if (align != _align)
                click_chatter("[%s] Updating alignment from %u to: %u", class_name(), _align, align);
            _align = align;
        }

        void get_counter_status(uint64_t &tot_aligned, uint64_t &tot_unaligned, uint64_t &total);

        void add_handlers();

    private:
        struct MemStats {
            uint64_t aligned_pkt_count;
            uint64_t total_pkt_count;

            MemStats() : aligned_pkt_count(0), total_pkt_count(0) {
            }

            inline uint64_t get_aligned_pkts() {
                return aligned_pkt_count;
            }

            inline uint64_t get_unaligned_pkts() {
                assert(total_pkt_count >= aligned_pkt_count);
                return (total_pkt_count - aligned_pkt_count);
            }

            inline uint64_t get_total_pkts() {
                return total_pkt_count;
            }

            void update_aligned_pkts(uint64_t ap = (uint64_t)1) {
                aligned_pkt_count += ap;
            }

            void update_total_pkts(uint64_t tp = (uint64_t)1) {
                total_pkt_count += tp;
            }

            void report_stats(unsigned thread_id) {
                click_chatter(
                    "[Thread %u] Aligned: %" PRIu64 ", Unaligned: %" PRIu64 ", Total: %" PRIu64,
                    thread_id, get_aligned_pkts(), get_unaligned_pkts(), get_total_pkts()
                );
            }
        };
        
        unsigned _align;
        per_thread<PacketMemStats::MemStats *> _stats;

        enum{
            h_aligned = 0, h_unaligned, h_total, h_align_stride, h_aligned_ratio, h_unaligned_ratio
        };

        inline bool mem_is_aligned(void *p, unsigned k = DEF_ALIGN) {
            return ((uintptr_t)p % k == 0);
        }

        static String read_handler(Element *e, void *thunk);
        static int write_handler(const String &, Element *, void *, ErrorHandler *) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
