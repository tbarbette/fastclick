#ifndef CLICK_CTXCRC_HH
#define CLICK_CTXCRC_HH
#include <click/batchelement.hh>
#include <click/flow/ctxelement.hh>
#include <click/flowbuffer.hh>
#include <click/simpledfa.hh>
CLICK_DECLS

struct fcb_crc {
    unsigned int crc = 0xffffffff;
    unsigned int remain = 0;
    unsigned int remainder = 0;
};

class CTXCRC : public StackChunkBufferElement<CTXCRC,fcb_crc> { //Use CTRP to avoid virtual
    public:

        CTXCRC() CLICK_COLD;
        ~CTXCRC() CLICK_COLD;

        const char *class_name() const      { return "CTXCRC"; }
        const char *port_count() const    { return PORTS_1_1X2; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int process_data(fcb_crc*, FlowBufferChunkIter&);

        virtual int maxModificationLevel(Element* stop) override {
            int r = StackChunkBufferElement<CTXCRC,fcb_crc>::maxModificationLevel(stop);
            //return r | MODIFICATION_STALL;
            return r;
        }

        /**
         * CRTP virtual
         */
       /* inline void release_stream(fcb_crc* fcb) {
            click_chatter("%u",fcb->crc);
        }*/

    private:
        static String read_handler(Element *, void *) CLICK_COLD;
        static int write_handler(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;
        bool _add;
};

CLICK_ENDDECLS
#endif
