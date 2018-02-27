#ifndef CLICK_FLOWCRC_HH
#define CLICK_FLOWCRC_HH
#include <click/batchelement.hh>
#include "stackelement.hh"
#include <click/flowbuffer.hh>
#include <click/simpledfa.hh>
CLICK_DECLS

struct fcb_crc {
    unsigned int crc = 0xffffffff;
    unsigned int remain = 0;
    unsigned int remainder = 0;
};

class FlowCRC : public StackChunkBufferElement<FlowCRC,fcb_crc> { //Use CTRP to avoid virtual
    public:

        FlowCRC() CLICK_COLD;
        ~FlowCRC() CLICK_COLD;

        const char *class_name() const      { return "FlowCRC"; }
        const char *port_count() const    { return PORTS_1_1X2; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int process_data(fcb_crc*, FlowBufferChunkIter&);

        virtual int maxModificationLevel() override {
            int r = StackChunkBufferElement<FlowCRC,fcb_crc>::maxModificationLevel();
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
