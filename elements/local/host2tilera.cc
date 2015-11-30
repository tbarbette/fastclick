// -*- c-basic-offset: 4 -*-
/*
 * reordertaggedarray.{cc,hh} -- queue element safe for use on SMP
 * Emmanouil Psanis
 *
 * Copyright (c) 2008 Meraki, Inc.
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
#include "host2tilera.hh"
CLICK_DECLS

#if 1
#define PACKET_SIZE                (4 * 1024)
#else
#define PACKET_SIZE                64
#endif

// Make sure that option 'pq_use_user_huge_pages=1' is used when you install
// the PCIe host driver, and hugetlb fs is mounted by running
// "mount none /huge/page -t hugetlbfs" as root before you turn on the
// following switch.
#if 0
#define USE_USER_HUGE_PAGES
#endif

// Arch x86_64 only supports up to two kinds of huge pages: 2MB and 1GB.
// Note that 1GB huge pages are only supported when there is 'pdpe1gb' flag
// in "/proc/cpuinfo".
#ifdef USE_USER_HUGE_PAGES
#define USE_1GB_HUGE_PAGES         0
#endif

#if 0
#define T2H_CHECK_DATA_PATTERN
#define H2T_GENERATE_DATA_PATTERN
#endif

#ifdef USE_USER_HUGE_PAGES
#define HPAGE_MNT_FILE             "/mnt/huge/buffer"
#define ROUNDUP(val, align)        (((val) + ((align) - 1)) & (~((align) - 1)))
// One 1GB huge page is reserved for the 512MB PQ ring buffer.
#if USE_1GB_HUGE_PAGES
#define MAP_LENGTH                 (512UL * 1024 * 1024)
#define HPAGE_SIZE                 (1024UL * 1024 * 1024)
#else
// Eight 2MB huge pages are reserved for the 16MB PQ ring buffer.
#define MAP_LENGTH                 (16UL * 1024 * 1024)
#define HPAGE_SIZE                 (2UL * 1024 * 1024)
#endif
#define NUM_SEGMENTS               (ROUNDUP(MAP_LENGTH, HPAGE_SIZE) / \
                                    HPAGE_SIZE)
#endif
// By default, the host ring buffer is 4MB in size and it consists of
// 1024 entries of 4KB packet buffers. Modify GXPCI_HOST_PQ_SEGMENT_ENTRIES
// in <asm/tilegxpci.h> to change the number of entries in the ring.
// For detailed ring buffer configuration information, refer to the
// comments in <asm/tilegxpci.h>.
#define RING_BUF_ELEMS             GXPCI_HOST_PQ_SEGMENT_ENTRIES
#ifndef USE_USER_HUGE_PAGES
#define RING_BUF_ELEM_SIZE         (HOST_PQ_SEGMENT_MAX_SIZE / \
                                    GXPCI_HOST_PQ_SEGMENT_ENTRIES)
#else
#define RING_BUF_ELEMS_TOTAL       (RING_BUF_ELEMS * NUM_SEGMENTS)
#define RING_BUF_ELEM_SIZE         (MAP_LENGTH / RING_BUF_ELEMS_TOTAL)
#endif


#ifdef GET_HOST_CPU_UTILIZATION
struct timeval busy_start;
struct timeval busy_end;
double busy_duration = 0;
int start_clk = 0;
#endif

#ifdef DEBUG
	#define PRINT(priority, ...) do{if (priority < DEBUG) click_chatter(__VA_ARGS__);}while(0);
#else
	#define PRINT(priority, ...) while(0){click_chatter(__VA_ARGS__);}
#endif
#if 1
#define WRITE
#endif

#if 0
#define BATCHED_UPDATES
#endif

#define MEASURE_THROUGHPUT
#if 0
#define likely(cond)               __builtin_expect(!!(cond), 1)
#define unlikely(cond)             __builtin_expect(!!(cond), 0)
#endif

Host2Tilera::Host2Tilera():host_ring_num_elems(RING_BUF_ELEMS),  host_buf_size(RING_BUF_ELEM_SIZE), data_offset(65 * sizeof(uint16_t) & 63 ? (65 * sizeof(uint16_t) & ~63) + 64 : 65 * sizeof(uint16_t)) {}

int
Host2Tilera::initialize(ErrorHandler *errh)
{
	char dev_name[40];
	#ifndef USE_USER_HUGE_PAGES
	  unsigned int host_ring_buffer_size = host_ring_num_elems * host_buf_size;
	#endif
	  //packet_size = PACKET_SIZE;
	  assert(4096 == host_buf_size);
	  //click_chatter("host_ring_num_elems = %d", host_ring_num_elems);
	  click_chatter("data_offset: %d", data_offset);
	  queue_index = 0;
	  // Open the packet queue file.
	  snprintf(dev_name, sizeof(dev_name), "/dev/tilegxpci%d/packet_queue/h2t/%d",
	           card_index, queue_index);
	  int pq_fd = open(dev_name, O_RDWR);
	  if (pq_fd < 0)
	  {
	    fprintf(stderr, "Host: Failed to open '%s': %s\n", dev_name,
	            strerror(errno));
	    return -1;
	  } else
		  click_chatter("Opened %s successfully", dev_name);

	  // mmap the register space.
	  pq_info.pq_regs = (struct gxpci_host_pq_regs_app*)
			  mmap(0, sizeof(struct gxpci_host_pq_regs_app),PROT_READ | PROT_WRITE, MAP_SHARED, pq_fd, TILEPCI_PACKET_QUEUE_INDICES_MMAP_OFFSET);
	  assert(pq_info.pq_regs != MAP_FAILED);

	  // Configure and allocate the ring buffer for the transmit queue.
	  tilepci_packet_queue_info_t buf_info;

	    // buf_size for h2t is used to pre-allocate the buffer pointers on the Tile
	    // side.
	    buf_info.buf_size = host_buf_size;

	  #ifdef USE_USER_HUGE_PAGES
	    buf_info.ring_size = MAP_LENGTH;
	    buf_info.hpage_size = HPAGE_SIZE;

	    // Create a file in hugetlb fs.
	    int hpage_fd = open(HPAGE_MNT_FILE, O_CREAT | O_RDWR);
	    if (hpage_fd < 0)
	    {
	      fprintf(stderr, "Host: Failed to open %s: %s\n",
	              HPAGE_MNT_FILE, strerror(errno));
	      return -1;
	    }

	    // Map the file into address space of current app's process.
	    void *hpage_addr = mmap(0, ROUNDUP(MAP_LENGTH, HPAGE_SIZE), PROT_READ | PROT_WRITE, MAP_SHARED, hpage_fd, 0);
	    if (hpage_addr == MAP_FAILED)
	    {
	      fprintf(stderr, "Host: Failed to map %s: %s\n",
	              HPAGE_MNT_FILE, strerror(errno));
	      return -1;
	    }

	    assert(hpage_addr != MAP_FAILED);
	    buf_info.ring_va = (intptr_t)hpage_addr;
	  #endif

	    int err = ioctl(pq_fd, TILEPCI_IOC_SET_PACKET_QUEUE_BUF, &buf_info);
	    if (err < 0)
	    {
	        fprintf(stderr, "Host: Failed TILEPCI_IOC_SET_PACKET_QUEUE_BUF: %s\n",
	                strerror(errno));
	        return -1;
	      }

	      // On the host side, mmap the transmmit queue region.
	    #ifdef USE_USER_HUGE_PAGES
	      pq_info.buffer = hpage_addr;
	    #else
	      pq_info.buffer = mmap(0, host_ring_buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, pq_fd, TILEPCI_PACKET_QUEUE_BUF_MMAP_OFFSET);
	      assert(pq_info.buffer != MAP_FAILED);
	      //click_chatter("Size of buffer: %d. From %p to %p", host_ring_buffer_size, pq_info.buffer, pq_info.buffer + host_ring_buffer_size);
	    #endif

	      // On the host side, mmap the queue status.
	      pq_info.pq_status= static_cast<tlr_pq_status *>(
	        mmap(0, sizeof(struct tlr_pq_status), PROT_READ | PROT_WRITE,
	             MAP_SHARED, pq_fd, static_cast<__off64_t>(TILEPCI_PACKET_QUEUE_STS_MMAP_OFFSET)));
	      assert(pq_info.pq_status != MAP_FAILED);

	      pq_info.pq_regs->producer_index = 0;
	      write = 0;
	      //host_min_pps = 0.0;
	      //card_index = 0;
	      pkts_written = 0;
	      batchNo = 0;
	      buffer_offset = 0;
	      full_counter = 0;
	      idx = 0;
	  //    pkt_sent = 0;
#ifdef SHOULD_DROP
	      dropped = 0;
#endif
#ifdef CORRECTNESS
	      data_pattern = 0;
#endif
	      //status = &(pq_info.pq_status->status);
	      //consumer_index = &(pq_info.pq_regs->consumer_index);
#if 0
	      HAVE_BATCH
	      click_chatter("Will push batch of packets");
#endif
	      return 0;
}

void *
Host2Tilera::cast(const char *n)
{
    if (strcmp(n, "Host2Tilera") == 0
	     || strcmp(n, "DiscardInterface") == 0)
    	return this;
    else
    	return 0;
}

int
Host2Tilera::configure(Vector<String> &conf, ErrorHandler *errh)
{
	unsigned int _packets = 0, _packet_size = PACKET_SIZE, _host_min_pps = 0, _queue_index = 0, _card_index = 0;
	bool _drop = false;
	if (Args(conf, this, errh)
			.read_p("PACKET_SIZE", _packet_size)
			.read_p("HOST_MIN_PPS", _host_min_pps)
			.read_p("QUEUE_INDEX", _queue_index)
			.read_p("CARD", _card_index)
			.read_p("PACKETS", _packets)
			.read_p("SHOULD_DROP", _drop)
			.complete() < 0)
		return -1;
//	if (_packets)
//		click_chatter("Packet limit: %d", _packets);
	packets = _packets;
	packet_size = _packet_size;
	host_min_pps = _host_min_pps;
	queue_index = _queue_index;
	card_index = _card_index;
	drop = _drop;
	return 0;
}

#if HAVE_BATCH
void Host2Tilera::push_batch(int, PacketBatch *batch){
    //int i = 0;
	{
	FOR_EACH_PACKET_SAFE(batch, p) {
		push(0,p);
		//click_chatter("Posting packet %d of batch %d", i++, batchNo++);
	}}
	FOR_EACH_PACKET_SAFE(batch, p) {
		p->kill();
	}
}
#endif

void Host2Tilera::push(int, Packet *p) {
#ifdef THROUGHPUT_MEASUREMENT
	unsigned long packet_count = 0;
#endif
	volatile uint32_t* consumer_index = &(pq_info.pq_regs->consumer_index);
	volatile uint32_t* producer_index = &(pq_info.pq_regs->producer_index);
	volatile enum gxpci_chan_status_t* status = &(pq_info.pq_status->status);
    //click_chatter("Pushing a packet of size %d\n", p->length());
	//bool printFlag = true;
    if (unlikely(*status == GXPCI_CHAN_RESET)) {
    	click_chatter("Host: H2T: channel is reset\n");
	    terminate();
	    abort();
	}
	// Determine the number of data buffers that have been consumed
	// by the Tile application.
    if (unlikely(0 == pkts_written)) {
    	//__sync_synchronize();
    	read = *consumer_index;
    	buffer_offset = data_offset;
    	length = (uint16_t *)(pq_info.buffer);
    	//click_chatter("Address of 1st page : %p", pq_info.buffer);
    }
    /*

	*/
	//click_chatter("Write = %d, read = %d", write, read);
	if ( 0 > host_ring_num_elems - (write - read) || read > write) {
		//click_chatter("Write = %d, read = %d", write, read);
		assert( 0 <= host_ring_num_elems - (write - read));
		assert(1024 == host_ring_num_elems);
		//assert (read <= write );
	}

	#ifdef GET_HOST_CPU_UTILIZATION
	    click_chatter("Host CPU utilization will be counted");
	    start_clk = 0;
	    if (host_ring_num_elems - (write - read))
	    {
	      start_clk = 1;
	      gettimeofday(&busy_start, NULL);
	    }
	#endif

	if (!drop) {
		if (unlikely (0 == host_ring_num_elems - (write - read))) {
			//__sync_synchronize();
			read = *consumer_index;
		}
		while(unlikely(0 == host_ring_num_elems - (write - read))) {
			if (unlikely(*status == GXPCI_CHAN_RESET)) {
				click_chatter("Host: H2T: channel is reset\n");
				terminate();
				abort();
			}
			/*
			if (printFlag) {
				click_chatter("Queue found full %d times, last packet inserted: %d", full_counter++, pkts_written);
				printFlag = false;
			}*/
			//__sync_synchronize();
			read = *consumer_index;
		}
	} else {
		if (unlikely(0 == host_ring_num_elems - (write - read))) {
			if (unlikely(*status == GXPCI_CHAN_RESET)) {
				click_chatter("Host: H2T: channel is reset\n");
	    		terminate();
	    		abort();
	    	}
	    	p->kill();
	    	++dropped;
	    	if (packets && packets == pkts_written+dropped) {
	    		flush_buffer_slice();
	    	//	    while(*consumer_index!=*producer_index){sleep(1);}//TODO: remove spin-waiting
	    	   	click_chatter("Exiting gracefully");
	    	   	terminate();
	    	}
	    	//sleep(1);
	    	return;
	    }
	}
#ifdef WRITE
#ifndef CORRECTNESS
	uint16_t l = static_cast<uint16_t>(p->length());
	//Align packet +length to cache line size on the host
	uint32_t l_order = (l & 63) ? (l & ~63) + 64 : l;  //ceiling to a multiple of 64
	//click_chatter("Length of packet: %d. Rounded to size: %x", l, l_order);
	if (buffer_offset + l_order > host_buf_size) {
#ifdef BATCHED_UPDATES
		if (unlikely(0 == (write & 0x3f) ))
			flush_buffer_slice();
		else {
			++write;
			buffer_offset = 0;
		}
#else
		flush_buffer_slice();
#endif
	}
	ptrn_ptr = (unsigned char *)(pq_info.buffer +
	        (write & (RING_BUF_ELEMS - 1)) * host_buf_size + buffer_offset);
	//click_chatter("Pattern pointer points to address %p", ptrn_ptr);
	length[++idx] = l;
	//*ptrn_ptr = p->length();
	//++ptrn_ptr;
	memcpy(ptrn_ptr, p->data(), l);
	//click_chatter("Writing a packet of size %d in address %p", l, ptrn_ptr);
	//++pkt_sent;
	buffer_offset += l_order;
#else
	uint32_t l = 128; //Need to set packet_size to equal value, either explicitly or via module configuration
	if (buffer_offset + l > host_buf_size)
		flush_buffer_slice();
	ptrn_ptr = (uint32_t *)(pq_info.buffer +
		        (write & (RING_BUF_ELEMS - 1)) * host_buf_size + buffer_offset);
	for (int j = 0; j < (packet_size >> 2); j++) {
		ptrn_ptr[j] = data_pattern++;
	}
	buffer_offset += l;
	//flush_buffer_slice();
#endif
#else
	if (0 == pkts_written%32+1 )
		flush_buffer_slice();
	p->kill();
#endif
	if (unlikely(0 == pkts_written)) {
		gettimeofday(&start, NULL);
	}
	++pkts_written;
	gigabits_per_sec += l*8;
	//termination measurements
	if (drop && packets && packets == pkts_written+dropped) {
	//if (packets && packets == pkts_written) {
		flush_buffer_slice();
	    //while(*consumer_index!=*producer_index){}//TODO: remove spin-waiting
	    click_chatter("Exiting gracefully");
	    terminate();
	    abort();
	} else if (packets && packets == pkts_written) {
		flush_buffer_slice();
			    //while(*consumer_index!=*producer_index){}//TODO: remove spin-waiting
			    click_chatter("Exiting gracefully");
			    terminate();
			    abort();
	}
	return;
}

inline void
Host2Tilera::flush_buffer_slice() {
	if(data_offset == buffer_offset)
		return;
	//if (4092 > buffer_offset) {
	//click_chatter("Flushing buffer writing %d", write+1);
	//ptrn_ptr = (uint32_t *)(pq_info.buffer +
	//	        (write & (RING_BUF_ELEMS - 1)) * host_buf_size + buffer_offset);
	//*ptrn_ptr = 0; //Signal than no more packets follow
	//}
    length[0] = idx;
    //click_chatter("Writing %d in offset %x, %x", idx, reinterpret_cast<void *>(pq_info.buffer + (write & (RING_BUF_ELEMS - 1)) * host_buf_size), length);
	volatile uint32_t* producer_index = &(pq_info.pq_regs->producer_index);
	idx = 0;

	//volatile uint32_t* consumer_index = &(pq_info.pq_regs->consumer_index);

	*producer_index = ++write;
	buffer_offset = data_offset;
	length = (uint16_t *)(pq_info.buffer + (write & (RING_BUF_ELEMS - 1)) * host_buf_size);
	//__sync_synchronize();
	//read = *consumer_index;
	//click_chatter("Flushing buffer with producer index %d and consumer index %d", write, read);
}

int
Host2Tilera::terminate()
{
//  unsigned long packet_count = 0;
  gettimeofday(&end, NULL);
  uint32_t read_l = pq_info.pq_regs->consumer_index;
  secs = end.tv_sec - start.tv_sec;
  usecs = end.tv_usec - start.tv_usec;

  while (usecs < 0)
  {
    usecs += 1000000;
    secs--;
  }
  uint32_t pkt_sent = (4096/128-1) * read_l; //packets per page(+header+alignment offset) * consumed pages
  duration = (double)secs + ((double)usecs) / 1000000;
  packets_per_sec = ((double)pkt_sent) / duration;
//  gigabits_per_sec = packets_per_sec * packet_size * 8;
  gigabits_per_sec /= duration;
  int packet_size = 114; //local override for printf
#ifdef GET_HOST_CPU_UTILIZATION
  printf("Host: Sent %lu %d-byte packets, taking %fs (real %fs), %.3fgbps, "
         "%.3fmpps\n", packet_count, packet_size, duration, busy_duration,
         gigabits_per_sec / 1e9, packets_per_sec / 1e6);
#else
if (drop)
	click_chatter("Host: Sent %lu %d-byte packets, taking %fs, %.3fgbps, %.3fmpps and dropped %d packets. In queue: %d\n",
		  pkt_sent, packet_size, duration, gigabits_per_sec / 1e9,
         packets_per_sec / 1e6, dropped, 4096/128 * (write-read_l));
else
  click_chatter("Host: Sent %lu %d-byte packets, taking %fs, %.3fgbps, %.3fmpps. In queue: %d\n",
  		  pkt_sent, packet_size, duration, gigabits_per_sec / 1e9,
           packets_per_sec / 1e6, 4096/128 * (write-read_l));

  click_chatter("Pages written: %d, pages read %d", write, read_l);
#endif

  // Fail if we didn't reach the minimum required performance.
  return (packets_per_sec < host_min_pps);
}
CLICK_ENDDECLS
//ELEMENT_REQUIRES(FullNoteQueue)
EXPORT_ELEMENT(Host2Tilera)
