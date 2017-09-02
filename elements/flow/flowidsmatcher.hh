#ifndef CLICK_FlowIDSMatcher_HH
#define CLICK_FlowIDSMatcher_HH
#include <click/batchelement.hh>
#include "../ids/regexset.hh"
#include "stackelement.hh"
#include <click/flowbuffer.hh>
#include <click/simpledfa.hh>
CLICK_DECLS


struct fcb_FlowIDSMatcher
{
    int state;
};

/*
=c
FlowIDSMatcher(PATTERN_1, ..., PATTERN_N)

=s
classifies packets by contents

=d

Matches packets against a set of Regex patterns.
The FlowIDSMatcher has 2 outputs, output 0 is for matched packets and output 1 (if connected)
is for unmatched packets.
Keyword arguments are:
=over 8

=item PAYLOAD_ONLY

Boolean. If set to true the pattern's will be matched against only the payload of the packet.
Default: false.

=item MATCH_ALL

boolean. If set to true the packet needs to be against all patterns. Default: false.

=back
=n

Patterns are standard Regex syntex as implemented by Google's re2 package.
For the full supported syntex see:
https://github.com/google/re2/wiki/Syntax

It's better to enclose each pattern with a pair of "

=e


For example,

  FlowIDSMatcher("xnet", "he.*o",);

Will match any packet with the word "xnet" or "he.*o" and will output it to port 0;


=h pattern0 rw
Returns or sets the element's pattern 0. There are as many C<pattern>
handlers as there are output ports.

=a RegexClassifier */
class FlowIDSMatcher : public StackBufferElement<FlowIDSMatcher,fcb_FlowIDSMatcher> { //Use CTRP to avoid virtual
	public:

		FlowIDSMatcher() CLICK_COLD;
		~FlowIDSMatcher() CLICK_COLD;

		const char *class_name() const 		{ return "FlowIDSMatcher"; }
		const char *port_count() const    { return PORTS_1_1X2; }

		int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
		void add_handlers() CLICK_COLD;
        int process_data(fcb_FlowIDSMatcher*, FlowBufferContentIter&);
	private:
		static String read_handler(Element *, void *) CLICK_COLD;
		static int write_handler(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;
		SimpleDFA _program;
};

CLICK_ENDDECLS
#endif
