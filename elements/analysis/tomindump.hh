// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_TOMINDUMP_HH
#define CLICK_TOMINDUMP_HH
#include <click/element.hh>
#include <click/task.hh>
#include <click/straccum.hh>
#include <click/notifier.hh>
CLICK_DECLS

#define MINDUMP_MAJOR_VERSION 0
#define MINDUMP_MINOR_VERSION 1

// Length of the fields
#define IP_L 4
#define PORT_L 2
#define LEN_L 2
#define FLAGS_L 1

// Total length of each entry
#define LINE_LEN (IP_L+IP_L+PORT_L+PORT_L+LEN_L+FLAGS_L)

// Offsets of each field in the entry
#define IPSRC_OFF (0)
#define IPDST_OFF (IPSRC_OFF+IP_L)
#define SPORT_OFF (IPDST_OFF+IP_L)
#define DPORT_OFF (SPORT_OFF+PORT_L)
#define LEN_OFF   (DPORT_OFF+PORT_L)
#define FLAGS_OFF (LEN_OFF+LEN_L)

// Pointer casting helpers
#define ASBYTEP(p) (reinterpret_cast<uint8_t *>(p)) 
#define ASWORDP(p) (reinterpret_cast<uint16_t *>(p)) 
#define ASDWORDP(p) (reinterpret_cast<uint32_t *>(p)) 
#define ASQWORDP(p) (reinterpret_cast<uint64_t *>(p)) 

#define ASCBYTEP(p) (reinterpret_cast<const uint8_t *>(p)) 
#define ASCWORDP(p) (reinterpret_cast<const uint16_t *>(p)) 
#define ASCDWORDP(p) (reinterpret_cast<const uint32_t *>(p)) 
#define ASCQWORDP(p) (reinterpret_cast<const uint64_t *>(p)) 

// Offsets for IPv4, no ethernet header
#define P_IPSRC_OFF (12)
#define P_IPDST_OFF (16)
#define P_SPORT_OFF (20)
#define P_DPORT_OFF (22)
#define P_LEN_OFF   (2)
#define P_PROTO_OFF (9)

// TODO: use flags field for additional data
#define PROTO2FLAGS(p) (p & 0xff)
#define FLAGS2PROTO(f) (f & 0xff)



/**

=c
ToMinDump(FILENAME [BURST])

=s traces
Generates a trace to be used with FromMinDump
=d
Keywords:
=over 8

=item FILENAME
The file to write

=item VERBOSE
Print more messages about the status. Default is 0.

=a FromMinDump, FromIPSummaryDump


=e 
FromDPDKDevice(0)
-> Strip(14)
-> ToMinDump(packets.mindump)
*/




class ToMinDump : public Element{ public:

    ToMinDump() CLICK_COLD;
    ~ToMinDump() CLICK_COLD;

    const char *class_name() const override	{ return "ToMinDump"; }
    const char *port_count() const override	{ return "1/0-1"; }
    const char *processing() const override	{ return "a/h"; }
    const char *flags() const		{ return "S2"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    void push(int, Packet *);
    bool run_task(Task *);

    String filename() const		{ return _filename; }
    uint32_t output_count() const	{ return _output_count; }

  private:

    String _filename;
    bool _verbose;
    FILE *_f;
    int32_t _binary_size;
    uint32_t _output_count;
    Task _task;
    NotifierSignal _signal;

    inline void write_packet(Packet* p);
    static int flush_handler(const String &, Element *, void *, ErrorHandler *);

};

CLICK_ENDDECLS
#endif
