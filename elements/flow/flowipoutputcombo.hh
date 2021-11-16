#ifndef CLICK_FLOWIPOUTPUTCOMBO_HH
#define CLICK_FLOWIPOUTPUTCOMBO_HH
#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/ip.h>

#include <click/flow/flowelement.hh>
CLICK_DECLS

/*
 * =c
 * FlowIPOutputCombo(COLOR, IPADDR, MTU)
 * =s ip
 * output combo for IP routing
 * =d
 * A single element encapsulating common tasks on an IP router's output path.
 * Effectively equivalent to
 *
 *   elementclass FlowIPOutputCombo { $COLOR, $IPADDR, $MTU |
 *     input[0] -> DropBroadcasts
 *           -> p::PaintTee($COLOR)
 *           -> g::IPGWOptions($IPADDR)
 *           -> FixIPSrc($IPADDR)
 *           -> d::DecIPTTL
 *           -> l::CheckLength($MTU)
 *           -> [0]output;
 *     p[1] -> [1]output;
 *     g[1] -> [2]output;
 *     d[1] -> [3]output;
 *     l[1] -> [4]output;
 *   }
 *
 * Output 0 is the path for normal packets; outputs 1 through 3 are error
 * outputs for PaintTee, IPGWOptions, and DecIPTTL, respectively; and
 * output 4 is for packets longer than MTU.
 *
 * =n
 *
 * FlowIPOutputCombo does no fragmentation. You'll still need an IPFragmenter for
 * that.
 *
 * =a DropBroadcasts, PaintTee, CheckLength, IPGWOptions, FixIPSrc, DecIPTTL,
 * IPFragmenter, IPInputCombo */

class FlowIPOutputCombo : public FlowSharedBufferPaintElement {

 public:

  FlowIPOutputCombo() CLICK_COLD;
  ~FlowIPOutputCombo() CLICK_COLD;

  const char *class_name() const		{ return "FlowIPOutputCombo"; }
  const char *port_count() const		{ return "1/5"; }
  const char *processing() const		{ return PUSH; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;



  void push_flow(int, int*, PacketBatch *);

 private:

  int _color;			// PaintTee
  struct in_addr _my_ip;	// IPGWOptions, FixIPSrc
  unsigned _mtu;		// Fragmenter

  inline int action(Packet* &p_in, bool color, int given_color);
};

CLICK_ENDDECLS
#endif
