#ifndef CLICK_STRINGCLASSIFIER_HH
#define CLICK_STRINGCLASSIFIER_HH
#include <click/batchelement.hh>
#include "ahocorasickplus.hh"
CLICK_DECLS

/*
=c
StringClassifier(STRING_1, ..., STRING_1)

=s classification
classifies packets by contents

=d

Classifies packets according to exect string.
The StringClassifier has N outputs, each associated with the corresponding string
from the configuration string. The pattern match will be done based on all the Packet's
content.

You should assume that the strings are scanned in order,
and the packet is sent to the output corresponding to the first matching pattern.
Thus more specific patterns should come before less specific ones.
If no match is found the packet is discarded.

=n
It's better to enclose each string with a pair of "

=e


For example,

  StringClassifier("xnet",
				  "hello",
				  "ynet");

creates an element with three outputs.
The first output will match a packet with the word xnet anywhere in it's content.
The second output will match a packet with the word hello anywhere in it's content.
The third output will match a packet with the word ynet anywhere in it's content.

=h pattern0 rw
Returns or sets the element's pattern 0. There are as many C<pattern>
handlers as there are output ports.

=a Classifier, IPFilter, CheckIPHeader, MarkIPHeader, CheckIPHeader2,
tcpdump(1) */

class StringClassifier : public BatchElement {
	public:
		StringClassifier() CLICK_COLD;
		~StringClassifier() CLICK_COLD;

		const char *class_name() const		{ return "StringClassifier"; }
		const char *port_count() const    { return "1/-"; }
		const char *processing() const    { return PUSH; }
		// this element does not need AlignmentInfo; override Classifier's "A" flag
		const char *flags() const     { return ""; }
		bool can_live_reconfigure() const   { return true; }

		int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
		void add_handlers() CLICK_COLD;

		int find_output(Packet *);

		void push      (int, Packet *);
	#if HAVE_BATCH
		void push_batch(int, PacketBatch *);
	#endif

	private:
		bool is_valid_patterns(Vector<String> &patterns, ErrorHandler *errh) const;
		static String read_handler(Element *, void *) CLICK_COLD;
		static int write_handler(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;
		AhoCorasick _matcher;
		Vector<String> _patterns;
		int _matches;
};


CLICK_ENDDECLS
#endif
