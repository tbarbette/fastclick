#ifndef LIBNICScheduler_HH
#define LIBNICScheduler_HH 1

#include <vector>
#include <string>
#include <utility>

//Dependencies to be moved from Click
#include <click/config.h>
#include <click/error.hh>
#include <click/timestamp.hh>
#if HAVE_NUMA
#include <click/numa.hh>
#endif
#include "../../elements/analysis/aggcountervector.hh"
#if HAVE_BPF
#include "../../elements/userlevel/xdploader.hh"
#endif

//NICScheduler library's includes
#include "ethernetdevice.hh"

class NICScheduler;

/**
 * Base virtual class to implement a balancing method
 * The balancing method
 */
class BalanceMethod { public:
    BalanceMethod(NICScheduler* b) : balancer(b) {
    }

    virtual void rebalance(std::vector<std::pair<int,float>> load) = 0;
    virtual int initialize(ErrorHandler *errh, int startwith);
    virtual void cpu_changed();
    virtual std::string name() = 0;

protected:

    NICScheduler* balancer;
};

//Base class for methods that access a device
class BalanceMethodDevice : public BalanceMethod { public:
    BalanceMethodDevice(NICScheduler* b, EthernetDevice* fd);

    EthernetDevice* _fd;
    bool _is_dpdk;
    friend class NICScheduler;
};

/**
 * Structure used by RSS++ to keep track of CPU usage and movements
 */
class LoadTracker {
    public:

    protected:
    int load_tracker_initialize(ErrorHandler* errh);

    std::vector<float> _past_load;
    std::vector<bool> _moved;
    std::vector<click_jiffies_t> _last_movement;
};


//All known methods
#include "methods/rss.hh"
#include "methods/rssrr.hh"
#include "methods/rsspp.hh"


class MigrationListener { public:
    MigrationListener();
    ~MigrationListener();

    //First : group id, second : destination cpu
    virtual void pre_migrate(EthernetDevice* dev, int from, std::vector<std::pair<int,int>> gids) = 0;
    virtual void post_migrate(EthernetDevice* dev, int from) = 0;

    virtual void init_assignment(unsigned* table, int sz) {};
};

/**
 * NICScheduler library class
 *
 * Maintains a set of used/unused cores, and a pointer to an ethernet device
 * (represented by a given instance of EthernetDevice). An external timer
 * calls the "balance" function to reassign some flows of the ethernet
 * device to different cores, and possibly automatically scale up
 * or down.
 *
 * NICScheduler can use multiple balancing method: RSS, a round-robin RSS or
 * the RSS++ algorithm presented at CoNEXT 2019, the default one.
 *
 * This class is not a standalone class, it is inteded to be integrated in
 * another framework or daemon. See FastClick's DeviceBalancer element
 * for an example of integration.
 *
 * A simple daemon would:
 * - Set up the EthernetDevice proxy to manipulate a table
 * - Define the BalanceMethod to be use
 * - Call balance() at a given frequency
 */
class NICScheduler {
public:

    NICScheduler();
    ~NICScheduler();

    /**
     * Define the balancing method to be use
     * @return -1 if the method was unknown, 0 in case of success
     */
    int set_method(std::string method, EthernetDevice* dev) {
        if (method == "pianorss" || method== "rss++" || method == "rsspp") {
            _method = new MethodRSSPP(this, dev);
        } else if (method == "rss") {
            _method = new MethodRSS(this, dev);
        } else if (method == "rssrr") {
            _method = new MethodRSSRR(this, dev);
        } else {
            return -1;
        }
        return 0;
    }

    /*
     * @returnthe balancing method
     */
    BalanceMethod* get_method() {
        return _method;
    }

    /**
     * Return the verbosity level of NICScheduler
     */
    inline int verbose() {
        return _verbose;
    }

    inline bool autoscale() {
        return _autoscale;
    }

    /**
     * Statistics to be kept for RSS++ method
     */
    struct RunStat {
        RunStat() : imbalance(0), count(0), time(0) {
        };
        double imbalance;
        int count;
        uint64_t time;
    };

    /**
     * Return the run statistics counter object for a given run index
     */
    inline RunStat& stats(int runid) {
        return _stats[runid];
    }

    /*
     * CPUInfo allows to keep the cycles value of a
     * CPU at a last tick
     */
    struct CpuInfo {
        int id;
        unsigned long long last_cycles;
    };

    /**
     * Return the informations of a given CPU by its id
     */
    CpuInfo& get_cpu_info(int id) {
        return _used_cpus[id];
    }

    /**
     * @return the number of used CPUs
     */
    inline int num_used_cpus() {
        return _used_cpus.size();
    }

    /**
     * @return the number of sparce CPUs
     */
    inline int num_spare_cpus() {
        return _available_cpus.size();
    }

    /**
     * @return the maximum number of cpus
     */
    inline int num_max_cpus() {
        return num_spare_cpus() + num_used_cpus();
    }

    /*
     * Find an available core and remove it from the available list
     * @return CPU id
     */
    int popAvailableCore();

    /**
     * Add a core from the available cores to the used cores
     */
    int addCore();

    /*
     * Remove a core using its physical CPU id
     */
    void removeCore(int phys_id);

    /**
     * Minimum tick period
     */
    int tick_min() {
        return _tick;
    }

    /**
     * Maximum tick period
     */
    int tick_max() {
        return _tick_max;
    }

    /**
     * Current tick period
     */
    int current_tick() {
        return _current_tick;
    }

    /**
     * Set the tick period
     */
    void set_tick(int tick) {
        _current_tick = tick;
    }

    // Parameters
    double overloaded_thresh() {
        return _overloaded_thresh;
    }

    double underloaded_thresh() {
        return _underloaded_thresh;
    }

    //Set the migration listener
    void set_migration_listener(MigrationListener* listener) {
        _manager = listener;
    }

    //The balancing method
    BalanceMethod* _method;

    //Flow manager
    MigrationListener* _manager;

    /**
     * User-defined function that will build a CpuInfo from a Cpu ID
     */
    virtual CpuInfo make_info(int id) = 0;

protected:

    // Parameters

    double _overloaded_thresh = 0.75;
    double _underloaded_thresh = 0.25;

    //Internal data structures

    //List of CPUs actually in use
    std::vector<CpuInfo> _used_cpus;

    //List of available CPUs
    std::vector<int> _available_cpus;

    //Per-run statistics
    std::vector<RunStat> _stats;

    //Is RSS++ actually active?
    bool _active;

    //Auto-scaling enabled?
    bool _autoscale;

    //Verbositiy mode. Useful for debugging
    int _verbose;

    //Min tick period
    int _tick;

    //Max speed period (higher is faster)
    int _tick_max;

    //Current tick period
    int _current_tick;

private:

    /*
     * Add a core in the set of used cores
     */
    void addCore(const CpuInfo& info);

};





#endif
