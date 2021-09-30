#ifndef CLICK_FLOWMINLOADSWITCH_HH
#define CLICK_FLOWMINLOADSWITCH_HH

#include <click/batchelement.hh>
#include <click/ipflowid.hh>
#include <click/hashtable.hh>

CLICK_DECLS

/*
 * =c
 * FlowMinLoadSwitch()
 *
 * =s classification
 * splits flows across its ports selecting the less-loaded one
 *
 * =d
 * Can have any number of outputs.
 * Chooses the output on which to emit each flow based on
 * on the number of packets already sent per-port..
 * The element use a hashtable to remember each 5-tuple
 * passing by and is therefore *not* thread-safe.
 *
 * =a
 * Switch, HashSwitch, StrideSwitch, RandomSwitch, IPRRSwitch
 */
class FlowMinLoadSwitch : public BatchElement {

    public:

        FlowMinLoadSwitch() CLICK_COLD;
        ~FlowMinLoadSwitch() CLICK_COLD;

        const char *class_name() const override { return "FlowMinLoadSwitch"; }
        const char *port_count() const override { return "1/1-"; }
        const char *processing() const override { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;
        void add_handlers() CLICK_COLD;

        static String read_handler(Element *handler, void *user_data);

        int process(int port, Packet *);
        void push(int port, Packet *);
    #if HAVE_BATCH
        void push_batch(int port, PacketBatch *);
    #endif

    private:

        class IPFlowSize {

            public:

                typedef IPFlowID key_type;
                typedef const IPFlowID &key_const_reference;

                IPFlowSize() {};
                IPFlowSize(uint32_t size) :
                    _size_bytes(size) {};

                void initialize(const IPFlowID &id) {
                    _id       = id;
                    _out_port = 0;
                }

                const IPFlowID &id() const {
                    return _id;
                }

                uint32_t size() {
                    return _size_bytes;
                }

                const uint8_t output_port() {
                    return _out_port;
                }

                void set_size(const uint32_t size) {
                    _size_bytes = size;
                }

                void update_size(const uint32_t extra_size) {
                    _size_bytes += extra_size;
                }

                void set_output_port(const uint8_t out_port) {
                    _out_port += out_port;
                }

                void set_mask(IPFlowID mask) {
                    _id = _id & mask;
                }

                key_const_reference hashkey() const {
                    return _id;
                }

            private:

                uint8_t  _out_port;
                IPFlowID _id;
                uint32_t _size_bytes;
        };

        /**
         * Total number of output ports.
         */
        uint8_t _max_nb_port;
        /**
         * Output port of the last flow seen.
         */
        uint8_t _current_port;
        /**
         * Flow table.
         */
        HashTable<IPFlowSize> _map;
        /**
         * Flow mask.
         */
        IPFlowID _mask;

        // Per-output port
        Vector<unsigned> _load;

        /**
         * Element's logic:
         * Assign a new flow to an output
         * port in a round-robin fashion.
         */
        unsigned round_robin() CLICK_WARN_UNUSED_RESULT;

        /**
         * Read handlers.
         */
        enum {
            h_flow_count
        };
};

CLICK_ENDDECLS

#endif
