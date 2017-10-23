#ifndef CLICK_FLOWIDSMATCHER_HH
#define CLICK_FLOWIDSMATCHER_HH
#include <click/batchelement.hh>
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
Block packets matching the content

=d



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

        virtual int maxModificationLevel() override {
            int r = StackBufferElement<FlowIDSMatcher,fcb_FlowIDSMatcher>::maxModificationLevel();
            if (_stall) {
                return r | MODIFICATION_STALL;
            } else {
                return r;
            }
        }
	private:
		static String read_handler(Element *, void *) CLICK_COLD;
		static int write_handler(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;
		SimpleDFA _program;
		bool _stall;
		atomic_uint32_t _stalled;
		atomic_uint32_t _matched;
};

CLICK_ENDDECLS
#endif
