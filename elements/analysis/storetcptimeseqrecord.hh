// -*- c-basic-offset: 4 -*-
#ifndef CLICK_STORETCPTIMESEQRECORD_HH
#define CLICK_STORETCPTIMESEQRECORD_HH
#include <click/batchelement.hh>

CLICK_DECLS

/*
=c

StoreTCPTimeSeqRecord([I<keywords> OFFSET, DELTA])

=s timestamps

=d

StoreTCPTimeSeqRecord element can be handy when computing end-to-end TCP packet
delays.  The element embeds a timestamp and a sequence number into a packet and
adjusts the checksum of the TCP packet.  Once the initial timestamp has been
placed into the payload of the TCP packet, a time difference can be computed
once a packet passes through another StoreTCPTimeSeqRecord element.  The data can be
accessed by examining the packet payload (e.g., do a tcpdump and then do post
processing of the data).  The element uses partial checksums to speed up the
processing.  The element works with IPv4 and IPv6 packets. In case of IPv6,
it assumes that the next header field is TCP.  If this is not the case, the
element will discard the packet.

Packet payload found after the TCP header.  Note, the TCP payload must be at least
22 bytes long.  The values will be stored in network byte order.

uint32_t seq_num
uint32_t initial_second
uint32_t initial_nano_second
uint32_t difference_second
uint32_t difference_nano_second

Keyword arguments are:

=over 2

=item OFFSET

Number of bytes to offset from the beginning of the packet where the IPv4/6 header can be found.
If raw Ethernet packets are fed into this element, then OFFSET needs to be 14.

=item DELTA

Determines which time values are set. If DELTA is false (the default), then the initial_second
and initial_nano_second values are set to the current time, and the difference values are set to 0.
If DELTA is true, then the initial values are left as is, and the difference values are set to
the difference between the current time and the packet's initial time.

=back
*/
class StoreTCPTimeSeqRecord : public BatchElement {
	public:
		StoreTCPTimeSeqRecord() CLICK_COLD;

		const char *class_name() const	{ return "StoreTCPTimeSeqRecord"; }
		const char *port_count() const	{ return PORTS_1_1; }

		void add_handlers() CLICK_COLD;
		int  configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;

		Packet      *simple_action      (Packet      *p);
	#if HAVE_BATCH
		PacketBatch *simple_action_batch(PacketBatch *batch);
	#endif

		// packet data payload access struct
		// Header | PDATA | rest of data
		// This comes out to 22 bytes which will fit into the smallest Ethernet frame
		struct PData {
			uint32_t seq_num;
			uint32_t data[4];
		};

	private:
		unsigned long _count;
		bool          _delta;  // if true put in_timestamp else out_timestamp
		uint32_t      _offset; //how much to shift to get to the IPv4/v6 header

		static String read_handler(Element *, void *) CLICK_COLD;
		static int    reset_handler(const String &, Element *, void *, ErrorHandler *);
};

CLICK_ENDDECLS
#endif
