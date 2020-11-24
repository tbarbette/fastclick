#ifndef EXACTCPUSWITCH_HH
#define EXACTCPUSWITCH_HH
#include <click/batchelement.hh>
#include <click/vector.hh>
CLICK_DECLS

/*
 * =c
 * ExactCPUSwitch()
 *
 * =s threads
 *
 * classifies packets by cpu
 *
 * =d
 *
 * Can have any number of outputs.
 * Chooses the output on which to emit each packet based on the thread's cpu. 
 * Contrary to CPUSwitch, the output is choosed using the thread-vector
 * mapping. It means that the outputs will be evenly shared among threads.
 * Eg, with CPUSwitch if threads 1, 7 and 8 push packets to a CPUSwitch with
 * 3 outputs, the 1 and 7 will push to output 1, and 8 to output 2. Leading to
 * an imbalance. 
 *
 * ExactCPUSwitch will use the known list of possible threads to balance
 * evenly one input thread per output port, using a static mapping computed
 * at initialization time.
 *
 * =a
 * CPUSwitch
 */

class ExactCPUSwitch : public BatchElement {

 public:

  ExactCPUSwitch() CLICK_COLD;
  ~ExactCPUSwitch() CLICK_COLD;

  const char *class_name() const override		{ return "ExactCPUSwitch"; }
  const char *port_count() const override		{ return "1/1-"; }
  const char *processing() const override		{ return PUSH; }

  int initialize(ErrorHandler* errh) override CLICK_COLD;
  int thread_configure(ThreadReconfigurationStage, ErrorHandler*, Bitvector threads) override CLICK_COLD;
  bool get_spawning_threads(Bitvector& b, bool, int) override;

  void update_map();

  void push(int port, Packet *);
#if HAVE_BATCH
  void push_batch(int port, PacketBatch *);
#endif

 private:
  Vector<unsigned> map;
};

CLICK_ENDDECLS
#endif
