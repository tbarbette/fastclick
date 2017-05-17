#ifndef CLICK_REGEXCLASSIFIER_HH
#define CLICK_REGEXCLASSIFIER_HH
#include <click/batchelement.hh>
#include "regexset.hh"
CLICK_DECLS

/*
=c
RegexClassifier(PATTERN_1, ..., PATTERN_N)

=s classification
classifies packets by contents

=d

Classifies packets according to Regex patterns.
The RegexClassifier has N outputs, each associated with the corresponding pattern
from the configuration string. The pattern match will be done based on all the Packet's
content.

You should assume that the patterns are scanned in order,
and the packet is sent to the output corresponding to the first matching pattern.
Thus more specific patterns should come before less specific ones.
If no match is found the packet is discarded.

=n

Patterns are standard Regex syntex as implemented by Google's re2 package.
For the full supported syntex see:
https://github.com/google/re2/wiki/Syntax

It's better to enclose each pattern with a pair of "

=e


For example,

  RegexClassifier("xnet",
				  "he.*o",
				  ".*");

creates an element with three outputs.
The first output will match a packet with the word xnet anywhere in it's content.
The second will match any packet which has a substring starting with he and endind with o.
The third pattern will match everything.

=h pattern0 rw
Returns or sets the element's pattern 0. There are as many C<pattern>
handlers as there are output ports.

=a Classifier, IPFilter, CheckIPHeader, MarkIPHeader, CheckIPHeader2,
tcpdump(1) */
class RegexClassifier : public BatchElement {
	public:

		RegexClassifier() CLICK_COLD;
		~RegexClassifier() CLICK_COLD;

		const char *class_name() const		{ return "RegexClassifier"; }
		const char *port_count() const    { return "1/-"; }
		const char *processing() const    { return PUSH; }
		// this element does not need AlignmentInfo; override Classifier's "A" flag
		const char *flags() const     { return ""; }
		bool can_live_reconfigure() const   { return true; }

		int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
		void add_handlers() CLICK_COLD;
		void push(int port, Packet *p);
	#if HAVE_BATCH
		void push_batch(int, PacketBatch *batch);
	#endif

	private:
		int find_output(Packet *p);
		bool is_valid_patterns(Vector<String> &patterns, ErrorHandler *errh) const;
		static String read_handler(Element *, void *) CLICK_COLD;
		static int write_handler(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;
		RegexSet *_program;
		bool _payload_only;
		int _max_mem;
};

CLICK_ENDDECLS
#endif
