// -*- c-basic-offset: 4 -*-
#ifndef CLICK_HOST2TILERA_HH
#define CLICK_HOST2TILERA_HH
#include <click/batchelement.hh>
#include <click/standard/storage.hh>
#include <click/sync.hh>
//#include "discardinterface.hh"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <asm/tilegxpci.h>
CLICK_DECLS
/*
=c

Host2Tilera
Host2Tilera(CAPACITY)

=s storage

stores packets in a FIFO queue

=d

Stores incoming packets in a first-in-first-out queue.
Drops incoming packets if the queue already holds CAPACITY packets.
The default for CAPACITY is 1000.

This variant of the default Queue is (should be) completely thread safe, in
that it supports multiple concurrent pushers and pullers.  In all respects
other than thread safety it behaves just like Queue, and like Queue it has
non-full and non-empty notifiers.

=h length read-only

Returns the current number of packets in the queue.

=h highwater_length read-only

Returns the maximum number of packets that have ever been in the queue at once.

=h capacity read/write

Returns or sets the queue's capacity.

=h drops read-only

Returns the number of packets dropped by the queue so far.

=h reset_counts write-only

When written, resets the C<drops> and C<highwater_length> counters.

=h reset write-only

When written, drops all packets in the queue.

=a Queue, SimpleQueue, NotifierQueue, MixedQueue, FrontDropQueue */
//#define DEBUG 6

/*
 * This modules requires lgxpci, lgxio and ltmc
 */
#if 0
#define CORRECTNESS
#endif

class Host2Tilera : public BatchElement { public:

    Host2Tilera() CLICK_COLD;

    const char *class_name() const		{ return "Host2Tilera"; }
    const char *port_count() const		{ return PORTS_1_0; }
    const char *processing() const		{ return PUSH; }
    void *cast(const char *);
    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;
    int initialize(ErrorHandler*) CLICK_COLD;
    int terminate();
    //int live_reconfigure(Vector<String> &conf, ErrorHandler *errh);
    void discarded( Packet *);
    inline void flush_buffer_slice();
  private:
#ifdef CORRECTNESS
    unsigned int data_pattern;
#endif
    // The "/dev/tilegxpci%d" device to be used.
    unsigned int card_index = 0;
    // Size of packet from the sender.
    unsigned int packet_size;
    // Number of buffers in the host ring buffer.
    const int  host_ring_num_elems;
    // Size of each buffer in the Tile-to-host ring buffer.
    unsigned int  host_buf_size;
    // Minimum packets-per-second, as measured by host.
    double host_min_pps = 0.0;
    // Packets used for test purposes
    unsigned int  packets, batchNo;
    // PCIe packet queue data for accessing packet queue data.
    struct host_packet_queue_t{
      struct tlr_pq_status* pq_status;
      struct gxpci_host_pq_regs_app* pq_regs;
      void* buffer;
      int pq_fd;
    } ;
    int queue_index, pkts_written;
    host_packet_queue_t pq_info;
    unsigned char* ptrn_ptr;
    uint32_t buffer_offset;
    uint32_t dropped;
  //volatile uint32_t* consumer_index;
  //volatile enum gxpci_chan_status_t* status;
  //bool postPage;
    uint16_t idx;
    uint16_t *length;
    const uint32_t data_offset;
  struct timeval start;
  struct timeval end;
  int secs, usecs;
  double duration, packets_per_sec, gigabits_per_sec;
  uint32_t read, write, full_counter;
  bool drop;

#if HAVE_INT64_TYPES
    typedef uint64_t counter_t;
#else
    typedef uint32_t counter_t;
#endif

#if HAVE_BATCH
    void push_batch(int port, PacketBatch *);
#endif
    void push(int port, Packet *);
};
CLICK_ENDDECLS
#endif
