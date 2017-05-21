#ifndef CLICK_GENERATEIPFILTER_HH
#define CLICK_GENERATEIPFILTER_HH
#include <click/batchelement.hh>
#include <click/ipflowid.hh>
#include <click/hashtable.hh>
CLICK_DECLS

/*
=c

GenerateIPFilter([LABEL, I<KEYWORDS>])

=s ip

pretty-prints IP packets

=d

Expects IP packets as input.  Should be placed downstream of a
CheckIPHeader or equivalent element.

Prints out IP packets in a human-readable tcpdump-like format, preceded by
the LABEL text.

Keyword arguments are:

=over 8

=item CONTENTS

Determines whether the packet data is printed. It may be `NONE' (do not print
packet data), `HEX' (print packet data in hexadecimal), or `ASCII' (print
packet data in plaintext). Default is `NONE'.

=item PAYLOAD

Like CONTENTS, but prints only the packet payload, rather than the entire
packet. Specify at most one of CONTENTS and PAYLOAD.

=item MAXLENGTH

If CONTENTS or PAYLOAD printing is on, then MAXLENGTH determines the maximum
number of bytes to print. -1 means print the entire packet or payload. Default
is 1500.

=item ID

Boolean. Determines whether to print each packet's IP ID field. Default is
false.

=item TTL

Boolean. Determines whether to print each packet's IP TOS field. Default is
false.

=item TOS

Boolean. Determines whether to print each packet's IP TOS field. Default is
false.

=item LENGTH

Boolean. Determines whether to print each packet's IP length field. Default is
false.

=item TIMESTAMP

Boolean. Determines whether to print each packet's timestamp in seconds since
1970. Default is true.

=item AGGREGATE

Boolean. Determines whether to print each packet's aggregate annotation.
Default is false.

=item PAINT

Boolean. Determines whether to print each packet's paint annotation. Default is false.

=item SWAP

Boolean. Determines whether to print ICMP sequence and ID numbers in
network order. Default is true. The RFC does not require these
two-byte values to be sent in any particular byte order. When Click
was originally designed, Linux/i386 wrote ping sequence numbers in
host byte order (often little endian), but it uses network order now.

=item OUTFILE

String. Only available at user level. PrintV<> information to the file specified
by OUTFILE instead of standard error.

=item ACTIVE

Boolean.  If false, then don't print messages.  Default is true.

=back

=a Print, CheckIPHeader */

class GenerateIPFilter : public BatchElement { public:

  GenerateIPFilter() CLICK_COLD;
  ~GenerateIPFilter() CLICK_COLD;

  const char *class_name() const		{ return "GenerateIPFilter"; }
  const char *port_count() const		{ return PORTS_1_1; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  int initialize(ErrorHandler *) CLICK_COLD;
  void cleanup(CleanupStage) CLICK_COLD;
  void add_handlers() CLICK_COLD;

  static String read_handler(Element *handler, void *user_data);

  Packet      *simple_action      (Packet      *p);
#if HAVE_BATCH
  PacketBatch *simple_action_batch(PacketBatch *batch);
#endif

 private:
  class IPFlow { public:

      typedef IPFlowID key_type;
      typedef const IPFlowID &key_const_reference;

      IPFlow() {
      }

      void initialize(const IPFlowID &flowid) {
          _flowid = flowid;
      }

      const IPFlowID &flowid() const {
          return _flowid;
      }


      void setMask(IPFlowID mask) {
          _flowid = _flowid & mask;
      }

      key_const_reference hashkey() const {
          return _flowid;
      }

    private:

      IPFlowID _flowid;
  };
  HashTable<IPFlow> _map;
  int _nrules;
  bool _keep_sport;
  bool _keep_dport;
  IPFlowID _mask;

};

CLICK_ENDDECLS
#endif
