#ifndef ExactPaintSwitch_HH
#define ExactPaintSwitch_HH
#include <click/batchelement.hh>
#include <click/vector.hh>
CLICK_DECLS

/*
 * =c
 * ExactPaintSwitch()
 *
 * =s standard
 *
 * classifies packets by paint annotation
 *
 * =d
 *
 * Can have any number of outputs.
 * Chooses the output on which to emit each packet based on packet's paint annotation.
 * Contrary to PaintSwitch, the output is choosed using a vector
 * mapping.
 *
 * =a
 * PaintSwitch
 */

class ExactPaintSwitch : public ClassifyElement<ExactPaintSwitch> {

 public:

  ExactPaintSwitch() CLICK_COLD;
  ~ExactPaintSwitch() CLICK_COLD;

  const char *class_name() const override		{ return "ExactPaintSwitch"; }
  const char *port_count() const override		{ return "1/1-"; }
  const char *processing() const override		{ return PUSH; }

  int configure(Vector<String> &conf, ErrorHandler *errh) override CLICK_COLD;

  int initialize(ErrorHandler* errh) override CLICK_COLD;

  int classify(Packet *);

 private:
  Vector<unsigned> map;
};

CLICK_ENDDECLS
#endif
