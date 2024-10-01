// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_COUNTERFILE_HH
#define CLICK_COUNTERFILE_HH
#include <click/batchelement.hh>
CLICK_DECLS

#define counter_int_type uint64_t
#define counter_atomic_int_type atomic_uint64_t

/*
=c

CounterFile(FILENAME, [I<keywords BATCH_PRECISE>])

=s counters

measures packet count and writes it to file

=d

Passes packets unchanged from its input to its output, maintaining statistics
information about packet count.

Keyword arguments are:

=over 8

=item FILENAME

Path to the file which stats will be written into. If the file does not exist,
it will be created. Parent directory of the file must exist. The file format is:

    struct stats {
        uint64_t _count;
        uint64_t _byte_count;
    };

Outside readers should mmap this file and access uint64_t fields of the structure.
They do not need to use atomic type (atomic_uint64_t) operations to access
variables, as they do not change them. Reading or writing a quadword (uint64_t)
aligned on a 64-bit boundary is guaranteed to be atomic on x86 and x64 since the
Pentium processor (1993). This may not be the case on other than x86 32-bit
architectures.

Reading using read() syscall instead of mmap is NOT guaranteed to be atomic.

If click is compiled with HAVE_MULTITHREAD, fields are 16-byte aligned.

=item BATCH_PRECISE

If true, will count packets one by one and hence call the handlers when the
count is exactly the limit. The default is to count the whole batch at once
and call the handlers if the limit has been reached.

=back

=h count read-only

Returns the number of packets that have passed through since the last reset.

=h byte_count read-only

Returns the number of bytes that have passed through since the last reset.

=h reset_counts write-only

Resets the counts and rates to zero.

=h reset write-only

Same as 'reset_counts'.

*/

/**
 * Ancestor of all types of counter so external elements can access count and byte_count
 * independently of the storage type
 */

class CounterFile : public BatchElement { public:

    CounterFile() CLICK_COLD;
    ~CounterFile() CLICK_COLD;

    const char *class_name() const override      { return "CounterFile"; }
    const char *processing() const override      { return AGNOSTIC; }
    const char *port_count() const override      { return PORTS_1_1; }

    counter_int_type count() {
        return _mmapped_stats->_count;
    }

    counter_int_type byte_count() {
        return _mmapped_stats->_byte_count;
    }

    struct stats {
        counter_int_type _count;
        counter_int_type _byte_count;
    };

    struct stats_atomic {
        counter_atomic_int_type _count;
        counter_atomic_int_type _byte_count;
    };

    /**
     * Read state in a non-atomic way, as fast as possible. Value of the ATOMIC
     *   parameter does not change this. Avoid locks, etc, just give a plausible
     *   value.
     */

    stats read() {
        return {_mmapped_stats->_count, _mmapped_stats->_byte_count};
    }

    /**
     * Atomic read. Atomicity level depends on the ATOMIC parameter.
     */

    stats atomic_read() {  //This is NOT atomic between the two fields
        return {_mmapped_stats->_count, _mmapped_stats->_byte_count};
    }

    /**
     * Add something to the counter, allowing stale and inconsistent data but
     *   still thread safe (the final total must be good)
     */

    void add(stats s) {
        _mmapped_stats->_count += s._count;
        _mmapped_stats->_byte_count += s._byte_count;
    }

    /**
     * Add in an atomic way.
     */

    void atomic_add(stats s) {
        _mmapped_stats->_count += s._count;
        _mmapped_stats->_byte_count += s._byte_count;
    }

    bool do_mt_safe_check(ErrorHandler* errh);

    //Only per-counter atomic
    int can_atomic() { return 1; } CLICK_COLD;

    void reset();

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch* batch);
#endif

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *) override CLICK_COLD;
    void cleanup(CleanupStage) override CLICK_COLD;
    void add_handlers() override CLICK_COLD;

  protected:

    bool _batch_precise;
    String _filename;
    stats_atomic *_mmapped_stats;
    static String read_handler(Element *, void *) CLICK_COLD;
    static int write_handler(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;
};

CLICK_ENDDECLS
#endif
