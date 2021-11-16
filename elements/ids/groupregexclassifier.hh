#ifndef CLICK_GROUPREGEXCLASSIFIER_HH
#define CLICK_GROUPREGEXCLASSIFIER_HH
#include <click/element.hh>
#include "regexset.hh"
CLICK_DECLS

/*
=c
GroupRegexClassifier("PATTERN_1" GROUP_NUMBER_1, ..., "PATTERN_N" GROUP_NUMBER_K)

=s classification
classifies packets by contents

=d

Classifies packets according to Regex patterns.
The GroupRegexClassifier has K outputs, each associated with a group of patterns
from the configuration string. The pattern match will be done based on all the Packet's
content.

The packet will be sent to an output corresponding to a group that matched all the patterns.
=n

Patterns are standard Regex syntex as implemented by Google's re2 package.
For the full supported syntex see:
https://github.com/google/re2/wiki/Syntax

It's better to enclose each pattern with a pair of "

=e


For example,

  GroupRegexClassifier("xnet" 0, "shopping" 0,"co" 1, "ynet" 1,".*" 2);

creates an element with three outputs.
The first output will match a packet with the word 'xnet' and the word 'shopping' anywhere in it's content.
The second will match any packet which has the word 'co' and the word 'ynet'.
The third pattern will match everything.

=h pattern0 rw
Returns or sets the element's pattern 0. There are as many C<pattern>
handlers as there are output ports.

=a RegexClassifier*/
class GroupRegexClassifier : public Element {
	public:

		GroupRegexClassifier() CLICK_COLD;
		~GroupRegexClassifier() CLICK_COLD;

		const char *class_name() const		{ return "GroupRegexClassifier"; }
		const char *port_count() const    { return "1/-"; }
		const char *processing() const    { return PUSH; }
		// this element does not need AlignmentInfo; override Classifier's "A" flag
		const char *flags() const     { return ""; }
		bool can_live_reconfigure() const   { return true; }

		int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
		void add_handlers() CLICK_COLD;

		void push(int, Packet *);

	private:
		bool is_valid_patterns(Vector<String> &patterns, ErrorHandler *errh) const;
		Vector<String> _patterns;
		Vector<int> _patterns_group_number;
		Vector<int> _groups_count;
		RegexSet _program;
};

CLICK_ENDDECLS
#endif
