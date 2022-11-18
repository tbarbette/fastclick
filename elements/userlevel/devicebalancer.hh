#ifndef CLICK_DEVICEBALANCER_HH
#define CLICK_DEVICEBALANCER_HH

#include <click/batchelement.hh>
#include <click/dpdkdevice.hh>
#include <nicscheduler/ethernetdevice.hh>
#include <nicscheduler/nicscheduler.hh>
#include "../analysis/aggcountervector.hh"

CLICK_DECLS

enum target_method {
    TARGET_LOAD,
    TARGET_BALANCE
};

enum load_method {
    LOAD_CPU,
	LOAD_CYCLES,
	LOAD_CYCLES_THEN_QUEUE,
    LOAD_QUEUE,
	LOAD_REALCPU
};



class FlowIPManager;
class DeviceBalancer;

/**
 *=title DeviceBalancer
 *
 *=c
 *
 *DeviceBalancer()
 *
 *=s threads
 *
 *Balance flows among cores using the NICScheduler library
 *
 *=d
 *
 *The DeviceBalancer element periodically calls
 *NICScheduler's balancing method to re-arrange flows-to-queue (and therefore core)
 *mapping of an Ethernet device, such as FromDevice, an external standard Linux
 *interface, or FromDPDKDevice.
 *The best known method supported by this element is "RSS++", see our
 *CoNEXT 2019 paper for more details.
 *
 *More details about the methods can be found in include/click/NICScheduler*
 *According to the result of the optimization, the element will re-program
 *the indirection table using the ethtool API.
 *
 *This elements needs --enable-rsspp and --enable-cpu-load to be given to the 
 *configure line to be built.
 *
 *This element extends NICScheduler*.
 *
 *Therefore if you're looking into the implementation, you should read
 *vendor/NICScheduler*
 *
 *Exemple usage :
 *
 *DeviceBalancer(DEV fd0, METHOD rsspp, VERBOSE $VERBOSE, STARTCPU 4, RSSCOUNTER agg, AUTOSCALE 0);
 *
 *=over 8
 *
 *=item DEV Element handling the device to balance
 *
 *=item METHOD Balancing method, eg rss or rsspp
 *
 *=item VERBOSE Verbosity level
 *
 *=item STARTCPU Number of CPU to use at startup
 *
 *=item RSSCOUNTER Element such as AggregateCounterVector to count packets per RSS index
 *
 *=item AUTOSCALE Enable autoscaling
 *
 *=back
 *
 *
 */

class DeviceBalancer : public Element, public NICScheduler {
public:

    DeviceBalancer() CLICK_COLD;
    ~DeviceBalancer() CLICK_COLD;

    const char *class_name() const { return "DeviceBalancer"; }
    const char *port_count() const { return "0/0"; }
    const char *processing() const { return AGNOSTIC; }

    int configure_phase() const {
        return CONFIGURE_PHASE_PRIVILEGED + 5;
    }

    bool can_live_reconfigure() const { return false; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *) override CLICK_COLD;
    void add_handlers() override CLICK_COLD;


    void run_timer(Timer* t) override;

    //Ignore a certain amount of cores not to be used
    int _core_offset;

    //The timer object, handling the tick call
    Timer _timer;

    //The target method, usually load
    target_method _target;

    //The load computation method, usually cyclesthenqueue
    load_method _load;

    //Number of cores to start with
    int _startwith;

    /*System CPU load is given as an absolute value, so we need to
    remember the value at th last tick to compute the current load.*/
    struct CPUStat {
		CPUStat() : lastTotal(0), lastIdle(0) {
		}
        unsigned long long lastTotal;
        unsigned long long lastIdle;
    };
    Vector<CPUStat> _cpustats;

    //User provided number of maximum CPUs
    unsigned _max_cpus;

	/**
     * Build a CpuInfo structure from a CPU id, using the current
     * router number of cycles for that CPU
     */
    virtual CpuInfo make_info(int id) override;

private:
    static int write_param(const String &in_s, Element *e, void *vparam, ErrorHandler *errh) CLICK_COLD;
    static String read_param(Element *e, void *thunk) CLICK_COLD;

};


CLICK_ENDDECLS

#endif
