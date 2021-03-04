#include <functional>
#include "nicscheduler.hh"

// NICScheduler functions

NICScheduler::NICScheduler() :  _verbose(0), _active(true), _manager(0), _autoscale(false) {

}

NICScheduler::~NICScheduler() {

}


int NICScheduler::popAvailableCore() {
    if (_available_cpus.size() < 1)
        return -1;
    int a_id = _available_cpus.back();
    _available_cpus.pop_back();
    return a_id;
}

void NICScheduler::addCore(const CpuInfo& info) {
    _used_cpus.push_back(info);
}

void NICScheduler::removeCore(int remove_phys_id) {
    _available_cpus.push_back(remove_phys_id);
    for (int uidx = 0; uidx < _used_cpus.size(); uidx++) {
        if (_used_cpus[uidx].id == remove_phys_id) {
                _used_cpus[uidx] = _used_cpus.back();
                _used_cpus.pop_back();
                return;
        }
    }
    assert(false);
}

/**
 * Add a core from the available cores to the used cores
 */
int NICScheduler::addCore() {
    int a_id = popAvailableCore();
    if (a_id < 0)
        return -1;

    const CpuInfo info = make_info(a_id);
    NICScheduler::addCore(info);
    return a_id;
}

// BalanceMethod

/**
 * Virtual function to initialize the method
 */
int BalanceMethod::initialize(ErrorHandler *errh, int startwith) {
    return 0;
};

/**
 * Virtual function called when a CPU assignment has changed. Default: do nothing
 */
void BalanceMethod::cpu_changed() {
};


// BalanceMethodDevice

/**
 * Constructor for BlanceMethodDevice (the virtual base for all device-based methods)
 */
BalanceMethodDevice::BalanceMethodDevice(NICScheduler* b, EthernetDevice* fd) : BalanceMethod(b), _fd(fd) {
    assert(_fd);
    assert(_fd->set_rss_reta);
    assert(_fd->get_rss_reta_size);
}

// MigrationListener
MigrationListener::MigrationListener() {

}

MigrationListener::~MigrationListener() {

}

// The methods

//Include code for all known methods
#include "methods/rss.cc"
#include "methods/rssrr.cc"
#include "methods/rsspp.cc"
#if HAVE_METRON
#include "methods/metron.cc"
#endif

int LoadTracker::load_tracker_initialize(ErrorHandler* errh) {
    _past_load.resize(click_max_cpu_ids());
    _moved.resize(click_max_cpu_ids(),false);

    _last_movement.resize(click_max_cpu_ids());
    click_jiffies_t now = click_jiffies();
    for (int i =0; i < _last_movement.size(); i++) {
        _last_movement[i] = now;
    }

    return 0;
}
