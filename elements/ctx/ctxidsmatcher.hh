#ifndef CLICK_CTXIDSMatcher_HH
#define CLICK_CTXIDSMatcher_HH
#include <click/batchelement.hh>
#include <click/flow/ctxelement.hh>
#include <click/flowbuffer.hh>
#include <click/simpledfa.hh>
CLICK_DECLS


struct fcb_CTXIDSMatcher
{
    int state;
};

/*
=c
CTXIDSMatcher(PATTERN_1, ..., PATTERN_N)

=s
Block packets matching the content

=d



=a RegexClassifier */
class CTXIDSMatcher : public StackBufferElement<CTXIDSMatcher,fcb_CTXIDSMatcher> { //Use CTRP to avoid virtual
	public:

		CTXIDSMatcher() CLICK_COLD;
		~CTXIDSMatcher() CLICK_COLD;

		const char *class_name() const 		{ return "CTXIDSMatcher"; }
		const char *port_count() const    { return PORTS_1_1X2; }

		int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
		void add_handlers() CLICK_COLD;
		int process_data(fcb_CTXIDSMatcher*, FlowBufferContentIter&);

        virtual int maxModificationLevel(Element* stop) override {
            int r = StackBufferElement<CTXIDSMatcher,fcb_CTXIDSMatcher>::maxModificationLevel(stop);
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


/**
 * Identical to CTXIDSMatcher but gives chunks to the iterator
 *
 */
class FlowIDSChunkMatcher : public StackChunkBufferElement<FlowIDSChunkMatcher,fcb_CTXIDSMatcher> { //Use CTRP to avoid virtual
    public:

        FlowIDSChunkMatcher() CLICK_COLD;
        ~FlowIDSChunkMatcher() CLICK_COLD;

        const char *class_name() const      { return "FlowIDSChunkMatcher"; }
        const char *port_count() const    { return PORTS_1_1X2; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        void add_handlers() CLICK_COLD;
        int process_data(fcb_CTXIDSMatcher*, FlowBufferChunkIter&);

        virtual int maxModificationLevel(Element* stop) override {
            int r = StackChunkBufferElement<FlowIDSChunkMatcher,fcb_CTXIDSMatcher>::maxModificationLevel(stop);
            return r;
        }
    private:
        static String read_handler(Element *, void *) CLICK_COLD;
        static int write_handler(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;
        SimpleDFA _program;
        atomic_uint32_t _stalled;
        atomic_uint32_t _matched;
};

CLICK_ENDDECLS
#endif
