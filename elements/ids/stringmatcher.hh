#ifndef CLICK_STRINGMATCHER_HH
#define CLICK_STRINGMATCHER_HH
#include <click/batchelement.hh>
#include "ahocorasickplus.hh"
CLICK_DECLS

/*
=c
StringMatcher(STRING_1, ..., STRING_1)

=s classification
Matches a packet based on a set of strings

=d

Tries to match a packet to one of the strings given in the configuration.
If a match is found the packet is sent to output 1. If nothing is connected it is
discarded. Packets which do not match any string are sent the output 0.

=e


For example,

	... -> sm::StringMatcher(xnet, text1, test3) -> Discard;
	sm[1] -> ...

Creates an element with 2 outputs.
If a packets matches any of the strings (xnet, text1 or test3) it will be send to output 1,
otherwise it will be discarded

=h matches read-only
Returns the number of matched packets.

=h reset_count write-only

When written, resets the C<matches>.
*/

class StringMatcher : public BatchElement {
	public:
		StringMatcher() CLICK_COLD;
		~StringMatcher() CLICK_COLD;

		const char *class_name() const		{ return "StringMatcher"; }
		const char *port_count() const    { return PORTS_1_1X2; }
		const char *processing() const    { return PROCESSING_A_AH; }
		// this element does not need AlignmentInfo; override Classifier's "A" flag
		const char *flags() const     { return ""; }
		bool can_live_reconfigure() const   { return true; }

		int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
		void add_handlers() CLICK_COLD;

		Packet      *simple_action      (Packet *);
	#if HAVE_BATCH
		PacketBatch *simple_action_batch(PacketBatch *);
	#endif

	private:
		bool is_valid_patterns(Vector<String> &, ErrorHandler *);
		static int write_handler(const String &, Element *e, void *thunk, ErrorHandler *errh) CLICK_COLD;
		AhoCorasick _matcher;
		Vector<String> _patterns;
		int _matches;
};


CLICK_ENDDECLS
#endif
