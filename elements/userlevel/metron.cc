// -*- c-basic-offset: 4; related-file-name: "metron.hh" -*-
/*
 * metron.{cc,hh} -- element that deploys, monitors, and (re)configures
 * high-performance NFV service chains driven by a remote controller
 *
 * Copyright (c) 2017 Tom Barbette, University of Liège and KTH Royal Institute of Technology
 * Copyright (c) 2017 Georgios Katsikas, KTH Royal Institute of Technology and RISE
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */
#include <click/config.h>

#include <click/args.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/userutils.hh>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <signal.h>
#include <limits>

#include "todpdkdevice.hh"
#include "metron.hh"

#include <metron/servicechain.hh>

#if HAVE_FLOW_API
    #include <click/flowrulemanager.hh>
#endif

#if HAVE_CURL
    #include <curl/curl.h>
#endif

CLICK_DECLS

/***************************************
 * Helper functions
 **************************************/
/**
 * Returns all the elements in a given array as
 * a space-separated string.
 */
const String
supported_types(const char **array)
{
    String supported;

    const uint8_t n = sizeof(array) / sizeof(array[0]);
    for (uint8_t i = 1; i < n; ++i) {
        supported += String(array[i]).lower() + " ";
    }

    return supported;
}

/**
 * Parses input string and returns information after key.
 */
static String
parse_info(const String &hw_info, const String &key)
{
    String s;

    s = hw_info.substring(hw_info.find_left(key) + key.length());
    int pos = s.find_left(':') + 2;
    s = s.substring(pos, s.find_left("\n") - pos);

    return s;
}


/**
 * Array of all CPU vendors.
 */
static const char *CPU_VENDORS_STR_ARRAY[] = { CPU_VENDORS };

/**
 * Converts an enum-based CPU vendor into string.
 */
const String
cpu_vendor_enum_to_str(const CpuVendor &c)
{
    String vendor_str = String(CPU_VENDORS_STR_ARRAY[static_cast<uint8_t>(c)]);
    vendor_str = vendor_str.lower().camel();
    int pos = vendor_str.find_left('_');
    return vendor_str.erase(pos, 1);
}

/**
 * Converts a string-based CPU vendor into enum.
 */
CpuVendor
cpu_vendor_str_to_enum(const String &c)
{
    const uint8_t n = sizeof(CPU_VENDORS_STR_ARRAY) /
                      sizeof(CPU_VENDORS_STR_ARRAY[0]);
    for (uint8_t i = 0; i < n; ++i) {
        Vector<String> tok = String(CPU_VENDORS_STR_ARRAY[i]).split('_');
        String cand = tok[0] + tok[1];

        if (strcmp(cand.upper().c_str(), c.upper().c_str()) == 0) {
            return (CpuVendor) i;
        }
    }
    return UNKNOWN_VENDOR;
}


/**
 * Array of all supported CPU cache types.
 */
static const char *CPU_CACHE_TYPES_STR_ARRAY[] = { CPU_CACHE_TYPES };

/**
 * Converts an enum-based CPU cache type into string.
 */
const String
cpu_cache_type_enum_to_str(const CpuCacheType &c)
{
    return String(CPU_CACHE_TYPES_STR_ARRAY[static_cast<uint8_t>(c)]).lower().camel();
}

/**
 * Converts a string-based CPU cache type into enum.
 */
CpuCacheType
cpu_cache_type_str_to_enum(const String &c)
{
    const uint8_t n = sizeof(CPU_CACHE_TYPES_STR_ARRAY) /
                      sizeof(CPU_CACHE_TYPES_STR_ARRAY[0]);
    for (uint8_t i = 0; i < n; ++i) {
        if (strcmp(CPU_CACHE_TYPES_STR_ARRAY[i], c.c_str()) == 0) {
            return (CpuCacheType) i;
        }
    }
    return UNKNOWN_CACHE_TYPE;
}


/**
 * Array of all supported CPU cache levels.
 */
static const char *CPU_CACHE_LEVELS_STR_ARRAY[] = { CPU_CACHE_LEVELS };

/**
 * Converts an enum-based CPU cache level into string.
 */
const String
cpu_cache_level_enum_to_str(const CpuCacheLevel &c)
{
    return String(CPU_CACHE_LEVELS_STR_ARRAY[static_cast<uint8_t>(c)]).upper();
}

/**
 * Converts a string-based CPU cache level into enum.
 */
CpuCacheLevel
cpu_cache_level_str_to_enum(const String &c)
{
    const uint8_t n = sizeof(CPU_CACHE_LEVELS_STR_ARRAY) /
                      sizeof(CPU_CACHE_LEVELS_STR_ARRAY[0]);
    for (uint8_t i = 0; i < n; ++i) {
        if (strcmp(CPU_CACHE_LEVELS_STR_ARRAY[i], c.c_str()) == 0) {
            return (CpuCacheLevel) i;
        }
    }
    return UNKNOWN_LEVEL;
}


/**
 * Array of all supported CPU cache policies.
 */
static const char *CPU_CACHE_POLICIES_STR_ARRAY[] = { CPU_CACHE_POLICIES };

/**
 * Converts an enum-based CPU cache policy into string.
 */
const String
cpu_cache_policy_enum_to_str(const CpuCachePolicy &c)
{
    String policy_str = String(CPU_CACHE_POLICIES_STR_ARRAY[static_cast<uint8_t>(c)]);
    return policy_str.lower().camel().replace('_', '-');
}

/**
 * Converts a string-based CPU cache policy into enum.
 */
CpuCachePolicy
cpu_cache_policy_str_to_enum(const String &c)
{
    const uint8_t n = sizeof(CPU_CACHE_POLICIES_STR_ARRAY) /
                      sizeof(CPU_CACHE_POLICIES_STR_ARRAY[0]);
    for (uint8_t i = 0; i < n; ++i) {
        if (strcmp(CPU_CACHE_POLICIES_STR_ARRAY[i], c.c_str()) == 0) {
            return (CpuCachePolicy) i;
        }
    }
    return UNKNOWN_POLICY;
}


/**
 * Array of all supported memory types.
 */
static const char *MEMORY_TYPES_STR_ARRAY[] = { MEMORY_TYPES };

/**
 * Converts an enum-based memory type into string.
 */
const String
memory_type_enum_to_str(const MemoryType &m)
{
    return String(MEMORY_TYPES_STR_ARRAY[static_cast<uint8_t>(m)]).upper();
}

/**
 * Converts a string-based memory type into enum.
 */
MemoryType
memory_type_str_to_enum(const String &m)
{
    const uint8_t n = sizeof(MEMORY_TYPES_STR_ARRAY) /
                      sizeof(MEMORY_TYPES_STR_ARRAY[0]);
    for (uint8_t i = 0; i < n; ++i) {
        if (strcmp(MEMORY_TYPES_STR_ARRAY[i], m.c_str()) == 0) {
            return (MemoryType) i;
        }
    }
    return UNKNOWN_MEMORY_TYPE;
}


/**
 * Array of all supported service chain types.
 */
static const char *SC_TYPES_STR_ARRAY[] = { SC_CONF_TYPES };

/**
 * Converts an enum-based service chain type into string.
 */
const String
sc_type_enum_to_str(const ScType &s)
{
    return String(SC_TYPES_STR_ARRAY[static_cast<uint8_t>(s)]).lower();
}

/**
 * Converts a string-based service chain type into enum.
 */
ScType
sc_type_str_to_enum(const String &sc_type)
{
    const uint8_t n = sizeof(SC_TYPES_STR_ARRAY) /
                      sizeof(SC_TYPES_STR_ARRAY[0]);
    for (uint8_t i = 0; i < n; ++i) {
        if (strcmp(SC_TYPES_STR_ARRAY[i], sc_type.c_str()) == 0) {
            return (ScType) i;
        }
    }
    return UNKNOWN;
}


/**
 * Array of all supported Rx filter types.
 */
static const char *RX_FILTER_TYPES_STR_ARRAY[] = { RX_FILTER_TYPES };

/**
 * Converts an enum-based Rx filter type into string.
 */
const String
rx_filter_type_enum_to_str(const RxFilterType &rf)
{
    return String(RX_FILTER_TYPES_STR_ARRAY[static_cast<uint8_t>(rf)]).lower();
}

/**
 * Converts a string-based Rx filter type into enum.
 */
RxFilterType
rx_filter_type_str_to_enum(const String &rf_str)
{
    const uint8_t n = sizeof(RX_FILTER_TYPES_STR_ARRAY) /
                      sizeof(RX_FILTER_TYPES_STR_ARRAY[0]);
    for (uint8_t i = 0; i < n; ++i) {
        if (strcmp(RX_FILTER_TYPES_STR_ARRAY[i], rf_str.c_str()) == 0) {
            return (RxFilterType) i;
        }
    }
    return NONE;
}


/******************************
 * PlatformInfo
 ******************************/
/**
 * PlatformInfo constructors.
 */
PlatformInfo::PlatformInfo() : _hw_info(), _sw_info(), _serial_nb(),_chassis_id()
{}

PlatformInfo::PlatformInfo(String hw, String sw, String serial, String chassis) :
    _hw_info(hw), _sw_info(sw), _serial_nb(serial), _chassis_id(chassis)
{}

/**
 * PlatformInfo destructor.
 */
PlatformInfo::~PlatformInfo()
{}

/**
 * Prints platform information.
 */
void
PlatformInfo::print()
{
    click_chatter("     H/W info: %s", get_hw_info().c_str());
    click_chatter("     S/W info: %s", get_sw_info().c_str());
    click_chatter("Serial number: %s", get_serial_number().c_str());
    click_chatter("   Chassis ID: %s", get_chassis_id().c_str());
}

/******************************
 * LatencyStats
 ******************************/
/**
 * LatencyStats constructor.
 */
LatencyStats::LatencyStats() : _avg_throughput(0), _min_latency(0),
                               _avg_latency(0), _max_latency(0)
{}

/**
 * LatencyStats destructor.
 */
LatencyStats::~LatencyStats()
{}

void
LatencyStats::print()
{
    click_chatter("\n");
    click_chatter("Average Throughput: %" PRIu64 " Mbps", _avg_throughput);
    click_chatter("Minimum    Latency: %" PRIu64 " nsec", _min_latency);
    click_chatter("Average    Latency: %" PRIu64 " nsec", _avg_latency);
    click_chatter("Maximum    Latency: %" PRIu64 " nsec", _max_latency);
}

/******************************
 * CPUStats
 ******************************/
/**
 * CPUStats constructor.
 */
CPUStats::CPUStats() : _physical_id(-1), _load(0.0f), _max_nic_queue(0),
                       _latency(), _active(false), _active_time()
{}

/**
 * CPUStats operator =.
 */
CPUStats &
CPUStats::operator=(CPUStats &r)
{
    _physical_id = r.get_physical_id();
    _load = r.get_load();
    _max_nic_queue = r.get_max_nic_queue();
    _latency = r.get_latency();
    _active = r.is_active();
    _active_time = r.get_active_time();

    return *this;
}

/**
 * CPUStats destructor.
 */
CPUStats::~CPUStats()
{}

/**
 * Retunrs the activity time of a CPU.
 */
int
CPUStats::active_since()
{
    int cpu_time;
    if (_active) {
        cpu_time = (Timestamp::now_steady() - _active_time).msecval();
    } else {
        cpu_time = -(Timestamp::now_steady() - _active_time).msecval();
    }
    return cpu_time;
}

/**
 * Prints CPU statistics.
 */
void
CPUStats::print()
{
    if (!is_assigned()) {
        return;
    }

    click_chatter("         CPU  Status: %s", _active ? "active" : "inactive");
    click_chatter("         CPU    Load: %f%%", _load);
    click_chatter("   Maximum NIC Queue: %d", _max_nic_queue);

    _latency.print();
}

/******************************
 * CPU
 ******************************/
/**
 * CPU constructor.
 */
CPU::CPU(CpuVendor vendor, int phy_id, int log_id,
         int socket, long frequency) : _stats()
{
    assert(vendor != UNKNOWN_VENDOR);
    assert(phy_id >= 0);
    assert(log_id >= 0);
    assert(socket >= 0);
    assert(frequency > 0);

    _vendor = vendor;
    _physical_id = phy_id;
    _logical_id = log_id;
    _socket = socket;
    _frequency = frequency;
}

/**
 * CPU destructor.
 */
CPU::~CPU()
{}

/**
 * Print CPU information.
 */
void
CPU::print()
{
    click_chatter("\n");
    click_chatter("         CPU  Vendor: %s", cpu_vendor_enum_to_str(get_vendor()).c_str());
    click_chatter("Physical CPU core ID: %d", _physical_id);
    click_chatter(" Logical CPU core ID: %d", _logical_id);
    click_chatter("       CPU Socket ID: %d", _socket);
    click_chatter("       CPU Frequency: %ld MHz", _frequency);

    _stats.print();
}

/**
 * Encodes CPU information to JSON.
 */
Json
CPU::to_json()
{
    Json cpu = Json::make_object();

    cpu.set("physicalId", get_physical_id());
    cpu.set("logicalId", get_logical_id());
    cpu.set("socket", get_socket());
    cpu.set("vendor", cpu_vendor_enum_to_str(get_vendor()));
    cpu.set("frequency", get_frequency());

    return cpu;
}

/******************************
 * CPU Cache ID
 ******************************/
/**
 * CpuCacheId constructors.
 */
CpuCacheId::CpuCacheId(CpuCacheLevel level, CpuCacheType type)
{
    assert(level != UNKNOWN_LEVEL);
    assert(type != UNKNOWN_CACHE_TYPE);
    _level = level;
    _type = type;
}

CpuCacheId::CpuCacheId(String level_str, String type_str) :
    CpuCacheId(
        cpu_cache_level_str_to_enum(level_str.upper()),
        cpu_cache_type_str_to_enum(type_str.upper())
    )
{}

/**
 * CpuCacheId destructor.
 */
CpuCacheId::~CpuCacheId()
{}


/******************************
 * CPU Cache
 ******************************/
/**
 * CpuCache constructors.
 */
CpuCache::CpuCache(CpuCacheLevel level, CpuCacheType type,
                   CpuCachePolicy policy, CpuVendor vendor,
                   long capacity, int sets, int ways, int line_length, bool shared)
{
    assert(policy != UNKNOWN_POLICY);
    assert(vendor != UNKNOWN_VENDOR);
    assert(capacity > 0);
    assert(sets > 0);
    assert(line_length > 0);
    assert(ways > 0);
    _cache_id = new CpuCacheId(level, type);
    _policy = policy;
    _vendor = vendor;
    _capacity = capacity;
    _sets = sets;
    _ways = ways;
    _line_length = line_length;
    _shared = shared;
}

CpuCache::CpuCache(
    String level_str, String type_str, String policy_str, String vendor_str,
    long capacity, int sets, int ways, int line_length, bool shared) :
    CpuCache(
        cpu_cache_level_str_to_enum(level_str.upper()),
        cpu_cache_type_str_to_enum(type_str.upper()),
        cpu_cache_policy_str_to_enum(policy_str.upper()),
        cpu_vendor_str_to_enum(vendor_str.upper()),
        capacity, sets, ways, line_length, shared
    )
{
}

/**
 * CpuCache destructor.
 */
CpuCache::~CpuCache()
{
    if (_cache_id)
        delete _cache_id;
}

/**
 * Turns an integer-based CPU cache level into CpuCacheLevel.
 */
CpuCacheLevel
CpuCache::level_from_integer(const int &level)
{
    switch(level) {
        case 0:
            return REGISTER;
        case 1:
            return L1;
        case 2:
            return L2;
        case 3:
            return L3;
        case 4:
            return L4;
        default:
            assert(false);
    }
}

/**
 * Turns a CpuCacheLevel into an integer-based CPU cache level.
 */
int
CpuCache::level_to_integer(const CpuCacheLevel &level)
{
    String level_str = cpu_cache_level_enum_to_str(level);
    return atoi(level_str.upper().split('L')[1].c_str());
}

/**
 * Turns a CpuCacheType into an string-based CPU cache type.
 */
CpuCacheType
CpuCache::type_from_string(const String &type)
{
    if (type.lower().equals("data")) {
        return DATA;
    } else if (type.lower().equals("instruction")) {
        return INSTRUCTION;
    }
    return UNIFIED;
}

/**
 * Turns an integer-based CPU cache policy into CpuCachePolicy.
 */
CpuCachePolicy
CpuCache::policy_from_ways(const int &ways, const int &sets)
{
    if (ways == 1) {
        return DIRECT_MAPPED;
    }

    if (sets == 1) {
        return FULLY_ASSOCIATIVE;
    }

    return SET_ASSOCIATIVE;
}

/**
 * Print the CPU cache layout.
 */
void
CpuCache::print()
{
    click_chatter("CPU Cache");
    click_chatter("\t     Vendor: %s", cpu_vendor_enum_to_str(get_vendor()).c_str());
    click_chatter("\t      Level: %s", cpu_cache_level_enum_to_str(get_cache_id()->get_level()).c_str());
    click_chatter("\t       Type: %s", cpu_cache_type_enum_to_str(get_cache_id()->get_type()).c_str());
    click_chatter("\t     Policy: %s", cpu_cache_policy_enum_to_str(get_policy()).c_str());
    click_chatter("\t  # of Sets: %d", get_sets());
    click_chatter("\t  # of Ways: %d", get_ways());
    click_chatter("\tLine length: %d Bytes", get_line_length());
    click_chatter("\t   Capacity: %ld kBytes", get_capacity());
    click_chatter("\t  Is shared: %s", is_shared() ? "Shared" : "Non-shared");
    click_chatter("\n");
}

/**
 * Encodes this CPU cache into JSON.
 */
Json
CpuCache::to_json()
{
    Json jcache = Json::make_object();

    jcache.set("vendor", cpu_vendor_enum_to_str(get_vendor()));
    jcache.set("level", cpu_cache_level_enum_to_str(get_cache_id()->get_level()));
    jcache.set("type", cpu_cache_type_enum_to_str(get_cache_id()->get_type()));
    jcache.set("policy", cpu_cache_policy_enum_to_str(get_policy()));
    jcache.set("sets", get_sets());
    jcache.set("ways", get_ways());
    jcache.set("lineLength", get_line_length());
    jcache.set("capacity", get_capacity());
    jcache.set("shared", is_shared() ? 1 : 0);

    return jcache;
}

/******************************
 * CPU Cache Hierarchy
 ******************************/
/**
 * CpuCacheHierarchy constructors.
 */
CpuCacheHierarchy::CpuCacheHierarchy(CpuVendor vendor, int sockets_nb, int cores_nb) :
    _levels(0), _per_core_capacity(0), _llc_capacity(0), _total_capacity(0)
{
    assert(vendor != UNKNOWN_VENDOR);
    assert(sockets_nb > 0);
    assert(cores_nb > 0);
    _vendor = vendor;
    _sockets_nb = sockets_nb;
    _cores_nb = cores_nb;

    query();
}

CpuCacheHierarchy::CpuCacheHierarchy(String vendor_str, int sockets_nb, int cores_nb) :
    CpuCacheHierarchy(cpu_vendor_str_to_enum(vendor_str), sockets_nb, cores_nb)
{}

/**
 * CpuCacheHierarchy destructor.
 */
CpuCacheHierarchy::~CpuCacheHierarchy()
{
    auto it = _cache_hierarchy.begin();
    while (it != _cache_hierarchy.end()) {
        if (it.value()) {
            delete it.value();
        }
        it++;
    }
    _cache_hierarchy.clear();
}

/**
 * Add a CPU cache into this hierarchy.
 *
 * @args cache: CPU cache to add into the hierarchy
 */
void
CpuCacheHierarchy::add_cache(CpuCache *cache)
{
    int currentSize = _cache_hierarchy.size();
    _cache_hierarchy.insert(cache->get_cache_id(), cache);
    assert(_cache_hierarchy.size() == currentSize + 1);

    int level = CpuCache::level_to_integer(cache->get_cache_id()->get_level());
    if (_levels < level) {
        _levels = level;
    }
    assert(_levels <= CpuCacheHierarchy::MAX_CPU_CACHE_LEVELS);

    long capacity = cache->get_capacity();
    assert(capacity > 0);
    if (cache->is_shared()) {
        _llc_capacity += capacity;
    } else {
        _per_core_capacity += capacity;
    }
}

/**
 * Initiate a query to retrieve the CPU cache layout.
 */
void
CpuCacheHierarchy::query()
{
    String cache_folders = shell_command_output_string(
        "find /sys/devices/system/cpu/cpu0/cache/ -maxdepth 1 -type d | grep index | sort -u", "", 0);
    Vector<String> cache_tokens = cache_folders.split('\n');
    for (unsigned i = 0; i < cache_tokens.size(); i++) {
        String cache_file = cache_tokens[i];
        if (cache_file.empty()) {
            continue;
        }

        int level_int = atoi(file_string(cache_file + "/level").c_str());
        bool is_shared = false;
        // String cpu_list = file_string(cache_file + "/shared_cpu_list").c_str();   // This might be false in case of HT
        // Vector<String> cpu_tokens = cpu_list.split(",-");
        if (level_int > 2) {
            is_shared = true;
        }
        String type_str = file_string(cache_file + "/type").trim_space().c_str();
        int sets = atoi(file_string(cache_file + "/number_of_sets").trim_space().c_str());
        int ways = atoi(file_string(cache_file + "/ways_of_associativity").trim_space().c_str());
        long capacity = atol(file_string(cache_file + "/size").trim_space().c_str()) * CpuCacheHierarchy::BYTES_IN_KILO_BYTE;
        int line_len = atoi(file_string(cache_file + "/coherency_line_size").trim_space().c_str());

        CpuCacheLevel level = CpuCache::level_from_integer(level_int);
        CpuCacheType type = CpuCache::type_from_string(type_str);
        CpuCachePolicy policy = CpuCache::policy_from_ways(ways, sets);

        add_cache(
            new CpuCache(
                level, type, policy, _vendor, capacity,
                sets, ways, line_len, is_shared
            )
        );
    }

    compute_total_capacity();
}

/**
 * Compute the total CPU cache capacity.
 * The total capacity is the sum of the (shared) socket-level
 * and the per-core capacities.
 */
void
CpuCacheHierarchy::compute_total_capacity()
{
    assert(_sockets_nb > 0);
    assert(_cores_nb > 0);
    assert(_per_core_capacity > 0);
    assert(_llc_capacity > 0);
    assert(_llc_capacity > _per_core_capacity);
    _total_capacity = (_sockets_nb * _llc_capacity) + (_cores_nb * _per_core_capacity);
    assert(
        (_total_capacity > 0) &&
        (_total_capacity > _per_core_capacity) &&
        (_total_capacity > _llc_capacity)
    );
}

/**
 * Print the CPU cache layout.
 */
void
CpuCacheHierarchy::print()
{
    if (_cache_hierarchy.size() == 0) {
        click_chatter("No CPU Cache hierarchy detected");
        return;
    }

    click_chatter("\n");
    click_chatter("=======================================================================");
    auto it = _cache_hierarchy.begin();
    while (it != _cache_hierarchy.end()) {
        CpuCache *cache = it.value();
        cache->print();
        it++;
    }

    click_chatter("CPU Cache Hierarchy - Summary");
    click_chatter("\tLevels: %d", _levels);
    click_chatter("\tVendor: %s", cpu_vendor_enum_to_str(_vendor).c_str());
    click_chatter("\t%3d CPU cores with (local) per-core capacity %10ld kB", _cores_nb, _per_core_capacity);
    click_chatter("\t%3d CPU sockets with per-socket LLC capacity %10ld kB", _sockets_nb, _llc_capacity);
    click_chatter("\tTotal capacity: %10ld kB", _total_capacity);
    click_chatter("=======================================================================");
}

/**
 * Encodes the CPU cache hierarchy into JSON.
 */
Json
CpuCacheHierarchy::to_json()
{
    Json jroot = Json::make_object();

    if (_cache_hierarchy.size() == 0) {
        click_chatter("No CPU Cache hierarchy to convert to JSON");
        return jroot;
    }

    jroot.set("sockets", get_sockets_nb());
    jroot.set("cores", get_cores_nb());
    jroot.set("levels", get_levels());

    // CPU cache resources
    Json jcaches = Json::make_array();

    auto it = _cache_hierarchy.begin();
    while (it != _cache_hierarchy.end()) {
        CpuCache *cache = it.value();
        jcaches.push_back(cache->to_json());

        it++;
    }

    jroot.set("cpuCaches", jcaches);

    return jroot;
}

/******************************
 * Memory Statistics
 ******************************/
/**
 * Main memory statistics constructor.
 */
MemoryStats::MemoryStats() : _used(0), _free(0), _total(0)
{
    capacity_query();
}

/**
 * Main memory statistics destructor.
 */
MemoryStats::~MemoryStats()
{}

/**
 * Initiate a query to retrieve the total memory capacity.
 */
void
MemoryStats::capacity_query()
{
    String mem_info = file_string("/proc/meminfo");
    long mem_total = atol(parse_info(mem_info, "MemTotal").c_str());
    assert(mem_total > 0);

    _total = mem_total;
}

/**
 * Initiate a query to retrieve the current memory utilization.
 */
void
MemoryStats::utilization_query()
{
    assert(_total > 0);

    String mem_info = file_string("/proc/meminfo");
    long mem_free = atol(parse_info(mem_info, "MemFree").c_str());
    long mem_used = _total - mem_free;
    assert(mem_used > 0);
    assert(mem_free > 0);
    assert(mem_used + mem_free == _total);

    _used = mem_used;
    _free = mem_free;
}

/**
 * Print main memory statistics.
 */
void
MemoryStats::print()
{
    click_chatter("\n");
    click_chatter("=======================================================================");
    click_chatter("Main Memory Utilization", get_memory_used_gb());
    click_chatter("Main Memory  Used: %ld GB", get_memory_used_gb());
    click_chatter("Main Memory  Free: %ld GB", get_memory_free_gb());
    click_chatter("Main Memory Total: %ld GB", get_memory_total_gb());
    click_chatter("=======================================================================");
}

/**
 * Encodes main memory statistics into JSON.
 */
Json
MemoryStats::to_json()
{
    // Give fresh stats to the controller
    utilization_query();

    Json jroot = Json::make_object();

    jroot.set("unit", "kBytes");
    jroot.set("used", get_memory_used());
    jroot.set("free", get_memory_free());
    jroot.set("total", get_memory_total());

    return jroot;
}

/******************************
 * Memory module
 ******************************/
/**
 * Main memory module constructor.
 */
MemoryModule::MemoryModule()
{
    _id = -1;
    _type = UNKNOWN_MEMORY_TYPE;
    _manufacturer = "";
    _serial_nb = "";
    _data_width = -1;
    _total_width = -1;
    _capacity = -1;
    _speed = -1;
    _configured_speed = -1;
}

MemoryModule::MemoryModule(int id, MemoryType type, String manufacturer, String serial_nb,
    int data_width, int total_width, long capacity, long speed, long configured_speed)
{
    assert(id >= 0);
    assert(type != UNKNOWN_MEMORY_TYPE);
    assert(!manufacturer.empty());
    assert(!serial_nb.empty());
    assert(data_width > 0);
    assert(total_width > 0);
    assert(capacity > 0);
    assert(speed > 0);
    assert(configured_speed > 0);

    _id = id;
    _type = type;
    _manufacturer = manufacturer;
    _serial_nb = serial_nb;
    _data_width = data_width;
    _total_width = total_width;
    _capacity = capacity;
    _speed = speed;
    _configured_speed = configured_speed;
}

MemoryModule::~MemoryModule()
{
}

/**
 * Prints main memory module.
 */
void
MemoryModule::print()
{
    click_chatter("\n");
    click_chatter("Memory               ID: %d", get_id());
    click_chatter("Memory             type: %s", memory_type_enum_to_str(get_type()).c_str());
    click_chatter("Memory     manufacturer: %s", get_manufacturer().c_str());
    click_chatter("Memory        serial no: %s", get_serial_number().c_str());
    click_chatter("Memory       data width: %d bits", get_data_width());
    click_chatter("Memory      total width: %d bits", get_total_width());
    click_chatter("Memory         capacity: %ld MBytes", get_capacity());
    click_chatter("Memory            speed: %ld MT/s", get_speed());
    click_chatter("Memory configured speed: %ld MT/s", get_configured_speed());
}

/**
 * Encodes main memory module information into JSON.
 */
Json
MemoryModule::to_json()
{
    Json jroot = Json::make_object();

    jroot.set("type", memory_type_enum_to_str(get_type()));
    jroot.set("manufacturer", get_manufacturer());
    jroot.set("serial", get_serial_number());
    jroot.set("dataWidth", get_data_width());
    jroot.set("totalWidth", get_total_width());
    jroot.set("capacity", get_capacity());
    jroot.set("speed", get_speed());
    jroot.set("speedConfigured", get_configured_speed());

    return jroot;
}

/******************************
 * Memory
 ******************************/
/**
 * Main memory constructor.
 */
MemoryHierarchy::MemoryHierarchy() : _stats()
{
    capacity_query();

    _memory_modules.resize(_modules_nb, 0);
    for (unsigned i = 0; i < (unsigned) _modules_nb; i++) {
        _memory_modules[i] = new MemoryModule();
    }
    _total_capacity = 0;

    query();
}

/**
 * Main memory destructor.
 */
MemoryHierarchy::~MemoryHierarchy()
{
    for (unsigned i = 0; i < (unsigned) _memory_modules.size(); i++) {
        if (_memory_modules[i]) {
            delete _memory_modules[i];
        }
    }

    _memory_modules.clear();
}

/**
 * Queries the system to acquire the number of main memory modules.
 */
void
MemoryHierarchy::capacity_query()
{
    // TODO: What if not DDR-based?
    int mem_modules = atoi(shell_command_output_string("lshw -short -C memory | grep DDR | wc -l", "", 0).c_str());
    assert(mem_modules > 0);
    _modules_nb = mem_modules;
}

/**
 * Queries the system to acquire main memory information.
 */
void
MemoryHierarchy::query()
{
    String mem_summary = shell_command_output_string("dmidecode -t memory", "", 0);
    Vector<String> line_vec = mem_summary.split('\n');
    unsigned active_devices = 0;

    for (unsigned j = 0; j < line_vec.size(); j++) {
        if (active_devices == get_modules_number()) {
            break;
        }

        String line = line_vec[j];
        if (line.empty()) {
            continue;
        }

        if (line.find_left(':') < 0) {
            continue;
        }

        Vector<String> line_tokens = line.split(':');
        if (line_tokens.size() != 2) {
            continue;
        }

        line_tokens[0] = line_tokens[0].trim_space().trim_space_left();
        line_tokens[1] = line_tokens[1].trim_space().trim_space_left();

        if (line_tokens[0].equals("Total Width") && !line_tokens[1].equals("Unknown")) {
            _memory_modules[active_devices]->set_total_width(
                atoi(line_tokens[1].split(' ')[0].c_str())
            );
            continue;
        }

        if (line_tokens[0].equals("Data Width") && !line_tokens[1].equals("Unknown")) {
            _memory_modules[active_devices]->set_data_width(
                atoi(line_tokens[1].split(' ')[0].c_str())
            );
            continue;
        }

        if (line_tokens[0].equals("Size") && !line_tokens[1].equals("No Module Installed")) {
            _memory_modules[active_devices]->set_capacity(
                atol(line_tokens[1].split(' ')[0].c_str())
            );
            _total_capacity += _memory_modules[active_devices]->get_capacity();
            continue;
        }

        if (line_tokens[0].equals("Type") && !line_tokens[1].equals("Unknown")) {
            _memory_modules[active_devices]->set_type(memory_type_str_to_enum(line_tokens[1]));
            continue;
        }

        if (line_tokens[0].equals("Speed") && !line_tokens[1].equals("Unknown")) {
            _memory_modules[active_devices]->set_speed(
                atol(line_tokens[1].split(' ')[0].c_str())
            );
            continue;
        }

        if (line_tokens[0].equals("Manufacturer") && !line_tokens[1].equals("Not Specified")) {
            _memory_modules[active_devices]->set_manufacturer(line_tokens[1]);
            continue;
        }

        if (line_tokens[0].equals("Serial Number") && !line_tokens[1].equals("Not Specified")) {
            _memory_modules[active_devices]->set_serial_nb(line_tokens[1]);
            continue;
        }

        if (line_tokens[0].equals("Configured Clock Speed") && !line_tokens[1].equals("Unknown")) {
            _memory_modules[active_devices]->set_configured_speed(
                atol(line_tokens[1].split(' ')[0].c_str())
            );
            _memory_modules[active_devices]->set_id((int) active_devices);
            active_devices++;
            continue;
        }
    }

    fill_missing();
}

/**
 * Returns the first valid capacity of a memory module.
 */
long
MemoryHierarchy::get_module_capacity()
{
    long capacity = -1;

    for (unsigned i = 0; i < (unsigned) _memory_modules.size(); i++) {
        capacity = _memory_modules[i]->get_capacity();
        if (capacity > 0) {
            return capacity;
        }
    }

    return capacity;
}

/**
 * Returns the first valid speed of a memory module.
 */
long
MemoryHierarchy::get_module_speed()
{
    long speed = -1;

    for (unsigned i = 0; i < (unsigned) _memory_modules.size(); i++) {
        speed = _memory_modules[i]->get_speed();
        if (speed > 0) {
            return speed;
        }
    }

    return speed;
}

/**
 * Prints main memory hierarchy.
 */
void
MemoryHierarchy::fill_missing()
{
    for (unsigned i = 0; i < (unsigned) _memory_modules.size(); i++) {
        MemoryModule *module = _memory_modules[i];
        if (module->get_capacity() < 0) {
            module->set_capacity(get_module_capacity());
        }
        if (module->get_speed() < 0) {
            module->set_speed(get_module_speed());
        }
        if (module->get_serial_number().empty()) {
            module->set_serial_nb("Not Specified");
        }
    }
}

/**
 * Prints main memory hierarchy.
 */
void
MemoryHierarchy::print()
{
    click_chatter("=======================================================================");
    click_chatter("    # of memory modules: %d", get_modules_number());
    click_chatter("  Total memory capacity: %ld MBytes", get_total_capacity());
    for (unsigned i = 0; i < (unsigned) _memory_modules.size(); i++) {
        _memory_modules[i]->print();
    }
    click_chatter("=======================================================================");
}

/**
 * Encodes main memory information into JSON.
 */
Json
MemoryHierarchy::to_json()
{
    Json jroot = Json::make_object();

    Json jmodules = Json::make_array();
    for (unsigned i = 0; i < (unsigned) _memory_modules.size(); i++) {
        jmodules.push_back(_memory_modules[i]->to_json());
    }

    jroot.set("modules", jmodules);

    return jroot;
}

/******************************
 * NIC
 ******************************/
/**
 * NIC constructor.
 */
NIC::NIC(bool verbose) :
    _element(0), _index(-1), _verbose(verbose), mirror(0)
{}

/**
 * NIC copy constructor.
 */
NIC::NIC(const NIC &n)
{
    _index = n._index;
    _verbose = n._verbose;
    _element = n._element;
}

/**
 * NIC destructor.
 */
NIC::~NIC()
{}

/**
 * Sets the Click element that represents this NIC.
 */
void
NIC::set_element(Element *el)
{
    assert(el);
    _element = el;
}

/**
 * Sets the device's Click port index.
 */
void
NIC::set_index(const int &index)
{
    assert(index >= 0);
    _index = index;
}

/**
 * Sets the NIC's status through the respective element.
 */
void
NIC::set_active(const bool &active)
{
    assert(_element);
    cast()->set_active(active);
}

/**
 * Casts a NIC object to its Click counterpart.
 */
FromDPDKDevice *
NIC::cast()
{
    if (!get_element()) {
        return NULL;
    }
    return dynamic_cast<FromDPDKDevice *>(get_element());
}

#if HAVE_FLOW_API
/**
 * Returns a FlowRuleManager object associated with this NIC.
 */
FlowRuleManager *
NIC::get_flow_rule_mgr(int sriov)
{
    return FlowRuleManager::get_flow_rule_mgr(get_port_id() + sriov);
}

/**
 * Returns a FlowRuleCache object associated with this NIC.
 */
FlowRuleCache *
NIC::get_flow_rule_cache(int sriov)
{
    return get_flow_rule_mgr(sriov)->flow_rule_cache();
}
#endif

/**
 * Returns the number of queues per VF pool.
 */
int
NIC::queue_per_pool()
{
    int nb_vf_pools = atoi(call_rx_read("nb_vf_pools").c_str());
    if (nb_vf_pools == 0) {
        return 1;
    }

    return atoi(call_rx_read("nb_rx_queues").c_str()) / nb_vf_pools;
}

/**
 * Maps a physical CPU core ID to a hardware queue ID.
 */
int
NIC::phys_cpu_to_queue(int phys_cpu_id)
{
    assert(phys_cpu_id >= 0);
    return phys_cpu_id * (queue_per_pool());
}

/**
 * Encodes NIC information to JSON.
 * Depending on the value of the second argument,
 * this piece of information can either be generic
 * NIC information of NIC statistics.
 */
Json
NIC::to_json(const RxFilterType &rx_mode, const bool &stats)
{
    Json nic = Json::make_object();

    nic.set("name", get_name());
    nic.set("id", get_index());
    if (!stats) {
        nic.set("vendor", call_rx_read("vendor"));
        nic.set("driver", call_rx_read("driver"));
        nic.set("speed", call_rx_read("speed"));
        nic.set("status", call_rx_read("carrier"));
        nic.set("portType", call_rx_read("type"));
        nic.set("hwAddr", call_rx_read("mac").replace('-',':'));
        Json jtagging = Json::make_array();
        jtagging.push_back(rx_filter_type_enum_to_str(rx_mode));
        nic.set("rxFilter", jtagging);
    } else {
        nic.set("rxCount", call_rx_read("hw_count"));
        nic.set("rxBytes", call_rx_read("hw_bytes"));
        nic.set("rxDropped", call_rx_read("hw_dropped"));
        nic.set("rxErrors", call_rx_read("hw_errors"));
        nic.set("txCount", call_tx_read("hw_count"));
        nic.set("txBytes", call_tx_read("hw_bytes"));
        nic.set("txErrors", call_tx_read("hw_errors"));
    }

    return nic;
}

/**
 * Implements read handlers for a NIC.
 */
String
NIC::call_rx_read(String h)
{
    const Handler *hc = Router::handler(_element, h);

    if (hc && hc->visible()) {
        return hc->call_read(_element, ErrorHandler::default_handler());
    }

    return "undefined";
}

/**
 * Relays handler calls to a NIC's underlying TX element.
 */
String
NIC::call_tx_read(String h)
{
    // TODO: Ensure element type
    ToDPDKDevice *td = dynamic_cast<FromDPDKDevice *>(_element)->find_output_element();
    if (!td) {
        return "[NIC " + String(get_device_address()) + "] Could not find matching ToDPDKDevice";
    }

    const Handler *hc = Router::handler(td, h);
    if (hc && hc->visible()) {
        return hc->call_read(td, ErrorHandler::default_handler());
    }

    return "undefined";
}

/**
 * Relays handler calls to a NIC's underlying Rx element.
 */
int
NIC::call_rx_write(String h, const String input)
{
    FromDPDKDevice *fd = dynamic_cast<FromDPDKDevice *>(_element);
    if (!fd) {
        click_chatter("[NIC %s] Could not find matching FromDPDKDevice", get_device_address().c_str());
        return ERROR;
    }

    const Handler *hc = Router::handler(fd, h);
    if (hc && hc->visible()) {
        return hc->call_write(input, fd, ErrorHandler::default_handler());
    }

    click_chatter("[NIC %s] Could not find matching handler %s", get_device_address().c_str(), h.c_str());

    return ERROR;
}

/***************************************
 * CPULayout
 **************************************/
/**
 * CPULayout constructor.
 */
CPULayout::CPULayout(int sockets_nb, int cores_nb, int active_cores_nb,
                    String vendor_str, long frequency, String numa_nodes) :
    _lcore_to_phy_core(), _lcore_to_socket()
{
    assert(sockets_nb >= 0);
    assert(cores_nb >= 0);
    assert(cores_nb % sockets_nb == 0);
    assert((active_cores_nb >= 0) && (active_cores_nb <= cores_nb));
    assert((vendor_str) && (!vendor_str.empty()));
    assert(frequency > 0);
    assert((numa_nodes) && (!numa_nodes.empty()));
    _sockets_nb = sockets_nb;
    _cores_nb = cores_nb;
    _cores_per_socket = _cores_nb / _sockets_nb;
    _active_cores_nb = active_cores_nb;
    _vendor = cpu_vendor_str_to_enum(vendor_str);
    _frequency = frequency;
    _numa_nodes = numa_nodes;
    _cpus.resize(cores_nb, 0);

    compose();
}

/**
 * CPULayout destructor.
 */
CPULayout::~CPULayout()
{
    for (unsigned i = 0; i < (unsigned)_cpus.size() ; i++) {
        if (_cpus[i]) {
            delete _cpus[i];
        }
    }
    _cpus.clear();

    _lcore_to_phy_core.clear();
    _lcore_to_socket.clear();
}

/**
 * Compose the CPU layout by processing the input information.
 */
void
CPULayout::compose()
{
    assert(!_numa_nodes.empty());

    unsigned lcore = 0;
    Vector<String> tokens = _numa_nodes.split('\n');
    for (unsigned i = 0; i < (unsigned) tokens.size(); i++) {
        String token = tokens[i];
        if (token.empty()) {
            continue;
        }

        Vector<String> core_tokens = token.split(':');
        assert(core_tokens.size() == 3);

        int socket = atoi(core_tokens[1].trim_space_left().split(' ')[0].c_str());
        // This core ID shows the numbering within a single socket
        // If we have more than one sockets, the numbering resets to 0.
        // int core = atoi(core_tokens[2].trim_space_left().c_str());

        _lcore_to_phy_core.insert(lcore, lcore);
        _lcore_to_socket.insert(lcore, socket);
        lcore++;
    }

    for (int i = 0; i < _cores_nb; i++) {
        _cpus[i] = new CPU(
            _vendor, get_phy_core_by_lcore(i), i, get_socket_by_lcore(i), _frequency
        );
    }
}

/**
 * Print the CPU layout.
 */
void
CPULayout::print()
{
    click_chatter("\n");
    click_chatter("=======================================================================");
    click_chatter("CPU layout information:");
    auto it = _lcore_to_phy_core.begin();
    while (it != _lcore_to_phy_core.end()) {
        int lcore = it.key();
        int pcore = it.value();
        int socket = _lcore_to_socket[lcore];
        click_chatter("\t[Socket %d] Logical core %3d --> Physical core %3d", socket, lcore, pcore);
        it++;
    }

    click_chatter("\nCPU cores' information:");
    for (unsigned i = 0; i < (unsigned)_cpus.size() ; i++) {
        _cpus[i]->print();
    }
    click_chatter("=======================================================================");
}

/***************************************
 * SystemResources
 **************************************/
/**
 * SystemResources constructor.
 */
SystemResources::SystemResources(
    int sockets_nb, int cores_nb, int active_cores_nb, String vendor, long frequency,
    String numa_nodes, String hw, String sw, String serial, String chassis) :
    _plat_info(hw, sw, serial, chassis),
    _cpu_layout(sockets_nb, cores_nb, active_cores_nb, vendor, frequency, numa_nodes),
    _cpu_cache_hierarchy(vendor, sockets_nb, cores_nb), _memory_hierarchy()
{}

/**
 * SystemResources destructor.
 */
SystemResources::~SystemResources()
{}

String
SystemResources::get_cpu_vendor()
{
    return cpu_vendor_enum_to_str(_cpu_layout.get_cpu_core(0)->get_vendor());
}

/**
 * Print the system's resources.
 */
void
SystemResources::print()
{
    click_chatter("=================================================================================");
    click_chatter("=== System Information");
    click_chatter("=================================================================================");
    click_chatter("# CPU sockets: %d", get_cpu_sockets());
    click_chatter("   CPU vendor: %s", get_cpu_vendor().c_str());

    _plat_info.print();
    _cpu_layout.print();
    _cpu_cache_hierarchy.print();
    _memory_hierarchy.print();
    click_chatter("=================================================================================");
    click_chatter("\n");
}

/***************************************
 * Metron
 **************************************/
/**
 * Metron constructor.
 */
Metron::Metron() :
    _timer(this), _sys_res(0), _rx_mode(FLOW),
    _discover_timer(&discover_timer, this),
    _discover_ip(), _discovered(false),
    _monitoring_mode(false), _fail(false),
    _load_timer(1000), _verbose(false)
{
    _core_id = click_max_cpu_ids() - 1;
    _cpu_click_to_phys.resize(click_max_cpu_ids(), 0);

    // Build up system resources
    collect_system_resources();
}

/**
 * Metron destructor.
 */
Metron::~Metron()
{
    if (_sys_res) {
        delete _sys_res;
    }

    _nics.clear();

    auto sci = _scs.begin();
    while (sci != _scs.end()) {
        if (sci.value()) {
            delete sci.value();
        }
        sci++;
    }
    _scs.clear();

    for (unsigned i = 0; i < _sc_to_core_map.size() ; i++) {
        if (_sc_to_core_map[i]) {
            delete _sc_to_core_map[i];
        }
    }
    _sc_to_core_map.clear();

    _args.clear();
    _dpdk_args.clear();
    _cpu_click_to_phys.clear();
}

/**
 * Collect important system resources.
 */
void
Metron::collect_system_resources()
{
    int sockets_nb = atoi(
        shell_command_output_string(
            "cat /proc/cpuinfo | grep 'physical id' | sort -u | wc -l", "", 0
        ).c_str()
    );
    int total_cores_nb = atoi(
        shell_command_output_string(
            "cat /proc/cpuinfo | grep 'processor' | sort -u | wc -l", "", 0
        ).c_str()
    );
    int active_cores_nb = click_max_cpu_ids();
    String cpu_info = file_string("/proc/cpuinfo");
    String cpu_vendor = parse_info(cpu_info, "vendor_id");
    String hw_info = parse_info(cpu_info, "model name");
    String sw_info = String("Click ") + String(CLICK_VERSION);
    String numa_nodes = shell_command_output_string(
        "egrep -e \"core id\" -e ^physical /proc/cpuinfo | xargs -l2 echo", "", 0);
    String sys_info = shell_command_output_string("dmidecode -t 1", "", 0);
    String serial = parse_info(sys_info, "Serial Number");
    String chassis_id = String(0); // Find a chassis ID :p
    long frequency = (long) cycles_hz() / CPU::MEGA_HZ;
    assert(frequency > 0);

    _sys_res = new SystemResources(
        sockets_nb, total_cores_nb, active_cores_nb,
        cpu_vendor, frequency, numa_nodes, hw_info,
        sw_info, serial, chassis_id
    );

    click_chatter("\n");
#if HAVE_DPDK
    if (dpdk_enabled) {
        unsigned id = 0;
        for (unsigned i = 0; i < RTE_MAX_LCORE; i++) {
            if (rte_lcore_is_enabled(i)) {
                click_chatter("Logical CPU core %d to %d", id, i);
                _cpu_click_to_phys[id++] = i;
            } else {
                click_chatter("Logical CPU core %d deactivated", i);
            }
        }
    } else
#endif
    {
        for (unsigned i = 0; i < _cpu_click_to_phys.size(); i++) {
            click_chatter("Logical CPU core %d to %d", i, i);
            _cpu_click_to_phys[i] = i;
        }
    }

    _sys_res->print();
}

/**
 * Configures Metron according to user inputs.
 */
int
Metron::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Vector<Element *> nics;
    String rx_mode;
    _agent_port         = DEF_AGENT_PORT;
    _discover_port      = _agent_port;
    _discover_rest_port = DEF_DISCOVER_REST_PORT;
    _discover_user      = DEF_DISCOVER_USER;
    _discover_path      = DEF_DISCOVER_PATH;
    _mirror = false;
    _slave_td_args = "";

    bool no_discovery = false;

    if (Args(conf, this, errh)
        .read    ("ID",                _agent_id)
        .read_all("NIC",               nics)
        .read    ("RX_MODE",           rx_mode)
        .read    ("AGENT_IP",          _agent_ip)
        .read    ("AGENT_PORT",        _agent_port)
        .read    ("DISCOVER_IP",       _discover_ip)
        .read    ("DISCOVER_PORT",     _discover_rest_port)
        .read    ("DISCOVER_PATH",     _discover_path)
        .read    ("DISCOVER_USER",     _discover_user)
        .read    ("DISCOVER_PASSWORD", _discover_password)
        .read    ("PIN_TO_CORE",       _core_id)
        .read    ("MONITORING",        _monitoring_mode)
        .read    ("FAIL",              _fail)
        .read    ("LOAD_TIMER",        _load_timer)
        .read    ("ON_SCALE", HandlerCallArg(HandlerCall::writable), _on_scale)
        .read    ("NODISCOVERY",       no_discovery)
        .read    ("MIRROR",            _mirror)
        .read    ("VERBOSE",           _verbose)
        .read_all("SLAVE_DPDK_ARGS",   _dpdk_args)
        .read_all("SLAVE_ARGS",        _args)
        .read    ("SLAVE_EXTRA",       _slave_extra)
        .read    ("SLAVE_TD_EXTRA",    _slave_td_args)
        .complete() < 0)
        return ERROR;

    // The CPU core ID where this element will be pinned
    unsigned max_core_nb = click_max_cpu_ids();
    if ((_core_id < 0) || (_core_id >= max_core_nb)) {
        return errh->error(
            "Cannot pin Metron agent to CPU core: %d. "
            "Use a CPU core index in [0, %d].",
            _core_id, max_core_nb
        );
    }

    // Set the Rx filter mode
    if (!rx_mode.empty()) {
        _rx_mode = rx_filter_type_str_to_enum(rx_mode.upper());

        if (_rx_mode == NONE) {
            return errh->error(
                "Supported Rx filter modes are: %s",
                supported_types(RX_FILTER_TYPES_STR_ARRAY).c_str()
            );
        }

    #if RTE_VERSION < RTE_VERSION_NUM(17,5,0,0)
        if (_rx_mode == FLOW) {
            return errh->error(
                "Rx filter mode %s requires DPDK 17.05 or higher",
                supported_types(RX_FILTER_TYPES_STR_ARRAY).c_str()
            );
        }
    #endif
    }

    if (_load_timer <= 0) {
        return errh->error("Set a positive scheduling frequency using LOAD_TIMER");
    }

    if (no_discovery) {
        _discovered = true;
    } else {
    #ifndef HAVE_CURL
        if (_discover_ip) {
            return errh->error(
                "Metron data plane agent requires controller discovery, "
                "but Click was compiled without libcurl support!"
            );
        }
    #endif

    #if HAVE_CURL
        // No discovery if key information is missing
        if (_discover_ip &&
            ((!_agent_ip) || (!_discover_user) || (!_discover_password))) {
            return errh->error(
                "Provide your local IP, a username, and a password to "
                "access Metron controller's REST API"
            );
        }

        // Ports must strictly become positive uint16_t
        if (        (_agent_port <= 0) ||         (_agent_port > UINT16_MAX) ||
            (_discover_rest_port <= 0) || (_discover_rest_port > UINT16_MAX)) {
            return errh->error("Invalid port number");
        }
    #endif
    }

    if (_monitoring_mode) {
        errh->warning(
            "Monitoring mode is likely to introduce performance degradation. "
            "In this mode, rate counters and timestamping elements are deployed on a per-core basis."
        );
    }

    // Setup pointers with the underlying NICs
    int index = 0;
    for (Element *e : nics) {
        NIC nic(_verbose);
        nic.set_index(index++);
        nic.set_element(e);
        _nics.insert(nic.get_name(), nic);
    }

    int status = confirm_nic_mode(errh);

    errh->message(
        "Rx filter mode: %s",
        rx_filter_type_enum_to_str(_rx_mode).c_str()
    );

    // Confirm the mode of the underlying NICs
    return status;
}

/**
 * Verifies that Metron configuration complies with the
 * underlying FromDPDKDevice elements' configuration.
 */
int
Metron::confirm_nic_mode(ErrorHandler *errh)
{
    bool confirmed = false;

    auto nic = _nics.begin();
    while (nic != _nics.end()) {
        // Cast input element
        FromDPDKDevice *fd = nic.value().cast();
        if (!fd || !fd->get_device()) {
            nic++;
            continue;
        }

        // Get its Rx mode
        String fd_mode = fd->get_device()->get_mode_str().empty() ? "unknown" : fd->get_device()->get_mode_str();

    #if HAVE_FLOW_API
        // TODO: What if none of the NICs is in Metron mode?
        if ((_rx_mode == FLOW) && (fd_mode != FlowRuleManager::DISPATCHING_MODE)) {
            errh->warning(
                "[NIC %s] Configured in MODE %s, which is incompatible with Metron's accurate dispatching",
                nic.value().get_name().c_str(), fd_mode.c_str()
            );
        } else if (_rx_mode == FLOW) {
            confirmed = true;
        }
    #endif

        // TODO: Implement VLAN tagging with VMDq
        if ((_rx_mode == VLAN) && (fd_mode == "vmdq")) {
            return errh->error(
                "[NIC %s] Metron RX_MODE %s based on %s is not supported yet",
                nic.value().get_name().c_str(), rx_filter_type_enum_to_str(_rx_mode).c_str(), fd_mode.c_str()
            );
        }

        if ((_rx_mode == MAC) && (fd_mode != "vmdq")) {
            return errh->error(
                "[NIC %s] Metron RX_MODE %s requires FromDPDKDevice MODE vmdq",
                nic.value().get_name().c_str(), rx_filter_type_enum_to_str(_rx_mode).c_str()
            );
        }

        if ((_rx_mode == RSS) && (fd_mode != "rss")) {
            return errh->error(
                "[NIC %s] RX_MODE %s is the default FastClick mode and requires FromDPDKDevice MODE rss. Current mode is %s",
                nic.value().get_name().c_str(), rx_filter_type_enum_to_str(_rx_mode).c_str(), fd_mode.c_str()
            );
        }

        confirmed = true;

        nic++;
    }

    if (!confirmed) {
        _rx_mode = NONE;
        errh->warning(
            "None of the NICs' Rx modes complies with Metron's dispatching, setting Rx mode to %s",
            rx_filter_type_enum_to_str(_rx_mode).c_str()
        );
    }

    return SUCCESS;
}

/**
 * Schedules periodic Metron controller discovery.
 */
void
Metron::discover_timer(Timer *timer, void *user_data)
{
    Metron *m = (Metron *) user_data;
    m->_discovered = m->discover();
    if (!m->_discovered) {
        m->_discover_timer.schedule_after_sec(m->DISCOVERY_WAIT);
    }
}

/**
 * Allocates memory resources before the start up
 * and performs controller discovery.
 */
int
Metron::initialize(ErrorHandler *errh)
{
    // Generate a unique ID for this agent, if not already given
    if (_agent_id.empty()) {
        _agent_id = "metron:nfv:dataplane:";
        String uuid = shell_command_output_string("cat /proc/sys/kernel/random/uuid", "", errh);
        uuid = uuid.substring(0, uuid.find_left("\n"));
        _agent_id = (!uuid || uuid.empty())? _agent_id + "00000000-0000-0000-0000-000000000001" : _agent_id + uuid;
    }

    if (_on_scale)
        if (_on_scale.initialize_write(this, errh) < 0)
            return -1;

    _sc_to_core_map.resize(get_cpus_nb(), 0);

#if HAVE_CURL
    // Only if user has requested discovery
    if (_discover_ip) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        _discovered = discover();
    }

    if (!_discovered) {
        if (_discover_ip) {
            _discover_timer.initialize(this);
            _discover_timer.move_thread(_core_id);
            _discover_timer.schedule_after_sec(DISCOVERY_WAIT);
            errh->warning("Could not send discovery message to Metron. Discovery will be done in background. Alternatively, the Metron controller can initiate the discovery");
        } else {
            errh->warning("To proceed, Metron controller must initiate the discovery");
        }
    }
#endif

    if (dpdk_enabled) {
        if (_nics.size() > 0) {
            assert(DPDKDevice::initialized());
        }

        if (try_slaves(errh) != SUCCESS) {
            return ERROR;
        }
    }

    _timer.initialize(this);
    _timer.move_thread(_core_id);
    _timer.schedule_after_msec(_load_timer);

    click_chatter("Successful initialization! \n\n");

    return SUCCESS;
}

/**
 * Cleans up static resources for service chains.
 */
int
Metron::static_cleanup()
{
    return SUCCESS;
}

/**
 * Checks whether the system is able to deploy
 * Metron slaves (i.e., secondary DPDK processes).
 */
int
Metron::try_slaves(ErrorHandler *errh)
{
    click_chatter("Checking the ability to run slaves...");

    ServiceChain sc(this);
    sc.id = "slaveTest";
    sc.config_type = CLICK;
    sc.config = "";
    sc.initialize_cpus(click_max_cpu_ids(), click_max_cpu_ids());
    for (unsigned i = 0; i < sc.get_nics_nb(); i++) {
        NIC *nic = sc.get_nic_by_index(i);
        sc._nics.push_back(nic);
    }
    sc._nic_stats.resize(sc._nics.size() * 1, NicStat());
    sc.rx_filter = new ServiceChain::RxFilter(&sc);
    Vector<int> cpu_phys_map;
    cpu_phys_map.resize(click_max_cpu_ids());
    assign_cpus(&sc, cpu_phys_map);
    assert(cpu_phys_map[0] >= 0);
    for (unsigned i = 0; i < click_max_cpu_ids(); i++) {
        sc.get_cpu_info(i).set_physical_id(cpu_phys_map[i]);
        sc.get_cpu_info(i).set_active(true);
    }
    sc._manager = new ClickSCManager(&sc, false);

    if (sc._manager->run_service_chain(errh) != SUCCESS) {
        return errh->error(
            "Unable to deploy Metron slaves: "
            "Please verify the compatibility of your NIC with DPDK secondary processes."
        );
    }

    kill_service_chain(&sc);
    unassign_cpus(&sc);

    click_chatter("Continuing initialization...");

    return SUCCESS;
}

/**
 * Advertizes Metron agent's features
 * though the REST port of the controller and not
 * through the port used by the Metron protocol
 * (usually default http).
 */
bool
Metron::discover()
{
#if HAVE_CURL
    CURL *curl;
    CURLcode res;

    /* Get a curl handle */
    curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "charsets: utf-8");

        // Compose the URL
        String url = "http://" + _discover_ip + ":" +
                    String(_discover_rest_port) + _discover_path;

        // Now specify the POST data
        Json j = Json::make_object();
        Json device;
        {
            Json rest = Json::make_object();
            rest.set("username", _agent_id);
            rest.set("password", "");
            rest.set("ip", _agent_ip);
            rest.set("port", _agent_port);
            rest.set("protocol", DEF_AGENT_PROTO);
            rest.set("url", "");
            rest.set("testUrl", "");
            rest.set("isProxy", false);
            sys_info_to_json(rest);
            device.set("rest", rest);

            Json basic = Json::make_object();
            basic.set("driver", DEF_DISCOVER_DRIVER);
            device.set("basic", basic);
        }
        Json devices = Json::make_object();
        devices.set("rest:" + _agent_ip + ":" + String(_agent_port), device);
        j.set("devices", devices);
        String s = j.unparse(true);

        // Curl settings
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, s.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, s.length());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        if (_discover_user) {
            String user_pass = _discover_user + ":" +  _discover_password;
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long) CURLAUTH_ANY);
            curl_easy_setopt(curl, CURLOPT_USERPWD, user_pass.c_str());
        }

        // Send the request and get the return code in res
        res = curl_easy_perform(curl);

        // Check for errors
        if(res != CURLE_OK) {
            click_chatter("Reattempting to connect to %s due to: %s\n",
                url.c_str(), curl_easy_strerror(res));
        } else {
            click_chatter("Discovery message: %s\n", s.c_str());
            click_chatter(
                "Successfully advertised features to Metron controller on %s:%d\n",
                _discover_ip.c_str(), _discover_rest_port
            );
        }

        // Always cleanup
        curl_easy_cleanup(curl);

        return (res == CURLE_OK);
    }
#endif

    return false;
}

/**
 * Metron agent's run-time.
 */
void
Metron::run_timer(Timer *t)
{
    auto sci = _scs.begin();

    while (sci != _scs.end()) {
        ServiceChain *sc = sci.value();
        if (sc && sc->_manager) {
            sc->_manager->run_load_timer();
        }
        sci++;
    }
    _timer.reschedule_after_msec(_load_timer);
}

/**
 * Releases memory resources before exiting.
 */
void
Metron::cleanup(CleanupStage)
{
#if HAVE_CURL
    curl_global_cleanup();
#endif
    // Delete service chains
    auto begin = _scs.begin();
    while (begin != _scs.end()) {
        delete begin.value();
        begin++;
    }
}

/**
 * Returns the number of CPU cores assigned to a service chain.
 */
int
Metron::get_assigned_cpus_nb()
{
    int tot = 0;
    for (unsigned i = 0; i < get_cpus_nb(); i++) {
        if (_sc_to_core_map[i] != 0) {
            tot++;
        }
    }

    return tot;
}

/**
 * Sets the CPU map of a given service chain.
 * @args sc: Service chain instance
 * @args map: A map to be filled with assigned cpu.
 *             The number of CPU asked for will be the map size.
 *
 * @return true on success
 */
bool
Metron::assign_cpus(ServiceChain *sc, Vector<int> &map)
{
    short offer = this->get_cpus_nb();
    short demand = this->get_assigned_cpus_nb() + map.size();

    if (demand > offer) {
        click_chatter(
            "Asked for %d CPU cores, but available CPU cores are %d",
            demand, offer
        );
        return false;
    }

    int j = 0;

    for (unsigned i = 0; i < _sc_to_core_map.size(); i++) {
        if (_sc_to_core_map[i] == 0) {
            _sc_to_core_map[i] = sc;
            map[j++] = i;
            if (j == map.size()) {
                return true;
            }
        }
    }

    return false;
}

/**
 * Resets the CPU map of a given service chain.
 */
void
Metron::unassign_cpus(ServiceChain *sc)
{
    for (unsigned i = 0; i < get_cpus_nb(); i++) {
        if (_sc_to_core_map[i] == sc) {
            _sc_to_core_map[i] = 0;
        }
    }
}

/**
 * Returns a service chain instance by looking up its ID
 * or NULL if no such service chain ID exists.
 */
ServiceChain *
Metron::find_service_chain_by_id(const String &id)
{
    return _scs.find(id);
}

/**
 * Calls the handler responsible for advertizing scaling events.
 * This occurs only when ON_SCALE parameter is set to true.
 */
void
Metron::call_scale(ServiceChain *sc, const String &event)
{
    if (_on_scale) {
        _on_scale.set_value(String(sc->get_active_cpu_nb()) + " " +
                  event + " " + sc->get_id() + " " +
                  String(sc->_total_cpu_load) + " " +
                  String(sc->_max_cpu_load) + " " +
                  String(sc->_max_cpu_load_index));
        _on_scale.call_write();
    }
}

/**
 * Assign CPUs to a service chain and run it.
 * If successful, the service chain is added to the internal service chains' list.
 * Upon failure, CPUs are unassigned.
 * It is the responsibility of the caller to delete the service chain upon an error.
 */
int
Metron::instantiate_service_chain(ServiceChain *sc, ErrorHandler *errh)
{
    assert(sc);

    Vector<int> cpu_phys_map;
    cpu_phys_map.resize(sc->get_max_cpu_nb());
    if (!assign_cpus(sc, cpu_phys_map)) {
        errh->error(
            "Cannot instantiate service chain %s: Not enough CPUs (SC wants %d cpus, Metron has %d left)",
            sc->get_id().c_str(),
            sc->get_max_cpu_nb(), get_assigned_cpus_nb()
        );
        return ERROR;
    }

    for (unsigned i = 0; i < (unsigned) cpu_phys_map.size(); i++) {
        assert(cpu_phys_map[i] >= 0);
        sc->get_cpu_info(i).set_physical_id(cpu_phys_map[i]);
    }

    for (unsigned i = 0; i < (unsigned) sc->_initial_cpus_nb; i++) {
        sc->get_cpu_info(i).set_active(true);
    }

    assert(sc->get_active_cpu_nb() == sc->_initial_cpus_nb);
    assert(sc->config_type != UNKNOWN);

    // Now create a manager for this new service chain
    if ((sc->config_type == MIXED) || (sc->config_type == CLICK)) {
        sc->_manager = new ClickSCManager(sc, true);
    } else {
        sc->_manager = new StandaloneSCManager(sc);
    }

    int ret = sc->_manager->run_service_chain(errh);
    if (ret != SUCCESS) {
        click_chatter("Could not launch service chain...");
        unassign_cpus(sc);
        if (_fail) {
            click_chatter("Metron failed to instantiate a service chain... It is going to abort due to a major error!");
            abort();
        }
        return ERROR;
    } else {
        click_chatter("Service chain launched successfully!");
    }

    call_scale(sc, "start");

    sc->status = ServiceChain::SC_OK;
    _scs.insert(sc->get_id(), sc);

    return SUCCESS;
}


/**
 * Kill the service chain without cleaning any ressource
 */
void
Metron::kill_service_chain(ServiceChain *sc)
{
    sc->_manager->kill_service_chain();
}

/**
 * Stop and delete a chain from the internal list, then unassign CPUs.
 * It is the responsibility of the caller to delete the chain.
 */
int
Metron::delete_service_chain(ServiceChain *sc, ErrorHandler *errh)
{
    // No controller
    if (!_discovered) {
        return errh->error(
            "Cannot delete service chain %s: Metron agent is not associated with a controller",
            sc->get_id().c_str()
        );
    }
    kill_service_chain(sc);
    unassign_cpus(sc);
    if (!_scs.remove(sc->get_id())) {
        return ERROR;
    }

    return SUCCESS;
}

/**
 * Schedule Metron agent's timer to connect to the controller.
 */
int
Metron::connect(const Json &j)
{
    if (!_discover_timer.scheduled()) {
        _discover_timer.initialize(this);
        _discover_timer.schedule_now();
    }
    _discovered = true;

    return SUCCESS;
}

/**
 * Un-schedule Metron agent's timer to disconnect from the controller.
 */
int
Metron::disconnect(const Json &j)
{
    // No controller
    if (!_discovered) {
        click_chatter(
            "Cannot disconnect from the controller: Metron agent is not associated with a controller"
        );
        return ERROR;
    }

    if (_discover_timer.scheduled())
        _discover_timer.unschedule();
    _discovered = false;

    return SUCCESS;
}

/**
 * Manage Metron agent's NIC ports through JSON.
 */
int
Metron::nic_port_administrator(const Json &j)
{
    // No controller
    if (!_discovered) {
        click_chatter(
            "Cannot manage NIC ports: Metron agent is not associated with a controller"
        );
        return ERROR;
    }

    // Get port command
    int port_number = j.get_i("port");
    String port_status = j.get_s("portStatus");

    click_chatter("Metron controller requested to %s NIC port %d", port_status.c_str(), port_number);

    NIC *nic = get_nic_by_index(port_number);
    if (!nic) {
        click_chatter("Cannot manage the status of unknown NIC port %d", port_number);
        return ERROR;
    }

    // Set the status of the port
    nic->set_active(port_status == "enable" ? true : false);

    return SUCCESS;
}

/**
 * Report Metron agent's NIC queues using JSON.
 */
Json
Metron::nic_queues_report()
{
    Json jroot = Json::make_object();

    // No controller
    if (!_discovered) {
        click_chatter(
            "Cannot report NIC queues: Metron agent is not associated with a controller"
        );
        return jroot;
    }

    click_chatter("Metron controller requested to report NIC queues");

    // An array of NICs
    Json jnics = Json::make_array();

    auto begin = _nics.begin();
    while (begin != _nics.end()) {
        NIC *nic = &begin.value();
        FromDPDKDevice *fd = nic->cast();
        DPDKDevice::DevInfo fd_info = fd->get_device()->get_info();

        Json jnic = Json::make_object();
        jnic.set("id", nic->get_index());

        // Each NIC requires an array of queues
        Json jqueues = Json::make_array();

        for (unsigned i = 0; i < (unsigned)fd_info.rx_queues.size(); i++) {
            // Active queue
            if (fd_info.rx_queues[i]) {
               Json jqueue = Json::make_object();
               jqueue.set("id", i);
               jqueue.set("type", "MAX");  // ONOS supports MIN, MAX, PRIORITY, BURST
               jqueue.set("maxRate", nic->call_rx_read("speed"));
               jqueues.push_back(jqueue);
            }
        }

        jnic.set("queues", jqueues);
        jnics.push_back(jnic);

        begin++;
    }

    jroot.set("nics", jnics);

    return jroot;
}

/**
 * Encodes hardware information to JSON.
 */
void
Metron::sys_info_to_json(Json &j)
{
    j.set("manufacturer", Json(_sys_res->get_cpu_vendor()));
    j.set("hwVersion", Json(_sys_res->get_hw_info()));
    j.set("swVersion", Json(_sys_res->get_sw_info()));
}

/**
 * Encodes Metron resources to JSON.
 */
Json
Metron::to_json()
{
    Json jroot = Json::make_object();

    click_chatter("Metron controller requested server's resources");

    jroot.set("id", Json(_agent_id));
    jroot.set("serial", Json(_sys_res->get_serial_number()));
    jroot.set("chassisId", Json(atol(_sys_res->get_chassis_id().c_str())));

    // System Info
    sys_info_to_json(jroot);

    // CPU resources
    Json jcpus = Json::make_array();
    for (unsigned i = 0; i < (unsigned) get_cpus_nb(); i++) {
        jcpus.push_back(
            _sys_res->get_cpu_layout().get_cpu_core(i)->to_json()
        );
    }
    jroot.set("cpus", jcpus);

    // CPU cache resources
    Json jcaches = _sys_res->get_cpu_cache_hiearchy().to_json();
    jroot.set("cpuCacheHierarchy", jcaches);

    // Main memory resources
    Json jmem = _sys_res->get_memory().to_json();
    jroot.set("memoryHierarchy", jmem);

    // NIC resources
    Json jnics = Json::make_array();
    auto begin = _nics.begin();
    while (begin != _nics.end()) {
        jnics.push_back(begin.value().to_json(this->_rx_mode, false));
        begin++;
    }
    jroot.set("nics", jnics);

    return jroot;
}

#if HAVE_FLOW_API
/**
 * Flushes all rules from all Metron NICs.
 */
int
Metron::flush_nics()
{
    auto it = _nics.begin();
    while (it != _nics.end()) {
        NIC *nic = &it.value();

        FlowRuleManager::get_flow_rule_mgr(nic->get_port_id())->flow_rules_flush();

        it++;
    }

    return SUCCESS;
}
#endif

/**
 * Encodes time to JSON.
 */
Json
Metron::time_to_json()
{
    Json jroot = Json::make_object();

    // No controller
    if (!_discovered) {
        click_chatter(
            "Cannot report time: Metron agent is not associated with a controller"
        );
        return jroot;
    }

    click_chatter("Metron controller requested the time of this server");

    // Enclose the timestamp into JSON
    jroot.set("time", Json(static_cast<long>(Timestamp::now().longval())));

    return jroot;
}

/**
 * Encodes system (e.g., CPU, NIC) statistics to JSON.
 */
Json
Metron::system_stats_to_json()
{
    Json jroot = Json::make_object();

    // No controller
    if (!_discovered) {
        click_chatter(
            "Cannot report global statistics: Metron agent is not associated with a controller"
        );
        return jroot;
    }

    click_chatter("Metron controller requested system statistics");

    // High-level CPU resources
    jroot.set("busyCpus", Json(get_assigned_cpus_nb()));
    jroot.set("freeCpus", Json(get_cpus_nb() - get_assigned_cpus_nb()));

    // Per core load
    Json jcpus = Json::make_array();

    /**
     * First, go through the active chains and search for
     * CPUs with some real load.
     * Mark them so that we can find the idle ones next.
     */
    int assigned_cpus = 0;
    Vector<int> busy_cpus;
    auto sci = _scs.begin();
    while (sci != _scs.end()) {
        ServiceChain *sc = sci.value();

        for (unsigned j = 0; j < sc->get_max_cpu_nb(); j++) {
            jcpus.push_back(sc->get_cpu_stats(j));
            busy_cpus.push_back(sc->get_cpu_phys_id(j));
            assigned_cpus++;
        }

        sci++;
    }

    // Now, inititialize the load of each idle core to 0
    for (unsigned j = 0; j < get_cpus_nb(); j++) {
        int *found = find(busy_cpus.begin(), busy_cpus.end(), (int) j);
        // This is a busy one
        if (found != busy_cpus.end()) {
            continue;
        }

        Json jcpu = Json::make_object();
        jcpu.set("id", j);
        jcpu.set("load", 0);      // No load
        jcpu.set("queue", -1);
        jcpu.set("busy", -1);  // This CPU core is free

        if (_monitoring_mode) {
            LatencyStats lat = LatencyStats();
            add_per_core_monitoring_data(&jcpu, lat);
        }

        jcpus.push_back(jcpu);
    }

    /*
     * At this point the JSON array should have load
     * information for each core of this server.
     */
    assert(jcpus.size() == get_cpus_nb());
    assert(assigned_cpus == get_assigned_cpus_nb());

    // CPU statistics
    jroot.set("cpus", jcpus);

    // Main memory statistics
    jroot.set("memory", _sys_res->get_memory().get_memory_stats().to_json());

    // NIC resources
    Json jnics = Json::make_array();
    auto begin = _nics.begin();
    while (begin != _nics.end()) {
        jnics.push_back(begin.value().to_json(this->_rx_mode, true));
        begin++;
    }
    jroot.set("nics", jnics);

    return jroot;
}

Json
Metron::setup_link_discovery()
{
    Json jroot = Json::make_object();

    // No controller
    if (!_discovered) {
        click_chatter(
            "Cannot perform link discovery: Metron agent is not associated with a controller"
        );
        return jroot;
    }

    click_chatter("Metron controller requested link discovery");

    click_chatter("Link discovery not supported yet");

    return jroot;
}

#if HAVE_FLOW_API
Json
Metron::nics_table_stats_to_json()
{
    Json jroot = Json::make_object();

    // No controller
    if (!_discovered) {
        click_chatter(
            "Cannot report NIC(s) table statistics: Metron agent is not associated with a controller"
        );
        return jroot;
    }

    click_chatter("Metron controller requested NIC(s) table statistics");

    // NIC resources
    Json jnics = Json::make_array();
    auto begin = _nics.begin();
    while (begin != _nics.end()) {
        NIC *nic = &begin.value();

        Json jnic = Json::make_object();
        jnic.set("id", nic->get_index());

        FlowRuleManager *fd = FlowRuleManager::get_flow_rule_mgr(nic->get_port_id());
        NicTableStats rule_stats(nic->get_port_id());
        fd->flow_rule_table_stats(rule_stats);

        // Arrays of NIC tables
        Json jtables = Json::make_array();

        Json jtable = Json::make_object();
        jtable.set("id", 0); // Currently all rules reside in the same table
        jtable.set("activeEntries", rule_stats.rules_nb());
        jtable.set("pktsLookedUp", rule_stats.pkts_looked_up());
        jtable.set("pktsMatched", rule_stats.pkts_matched());
        int32_t table_capacity = rule_stats.capacity();
        if (table_capacity > 0) {
            jtable.set("maxSize", table_capacity);
        }
        jtables.push_back(jtable);

        jnic.set("table", jtables);
        jnics.push_back(jnic);

        begin++;
    }
    jroot.set("nics", jnics);

    return jroot;
}
#endif

/**
 * Extends the input JSON object with additional fields.
 * These fields contain per-core measurements, such as
 * average throughput and several latency percentiles.
 */
void
Metron::add_per_core_monitoring_data(Json *jobj, LatencyStats &lat)
{
    if (!jobj) {
        click_chatter("Input JSON object is NULL. Cannot add per-core monitoring data");
        return;
    }

    if ((lat.get_avg_throughput() < 0) || (lat.get_min_latency() < 0) ||
        (lat.get_avg_latency() < 0) || (lat.get_max_latency() < 0)) {
        click_chatter("Invalid per-core monitoring data");
        return;
    }

    Json jtput = Json::make_object();
    jtput.set("average", lat.get_avg_throughput());
    jtput.set("unit", "bps");
    jobj->set("throughput", jtput);

    Json jlat = Json::make_object();
    jlat.set("min", lat.get_min_latency());
    jlat.set("average", lat.get_avg_latency());
    jlat.set("max", lat.get_max_latency());
    jlat.set("unit", "ns");
    jobj->set("latency", jlat);
}

/**
 * Encodes controller information to JSON.
 */
Json
Metron::controllers_to_json()
{
    Json jroot = Json::make_object();

    // No controller
    if (!_discovered) {
        return jroot;
    }

    click_chatter("Metron controller requested the controllers of this server");

    // A list with a single controller (always)
    Json jctrls_list = Json::make_array();
    Json jctrl = Json::make_object();
    jctrl.set("ip", _discover_ip);
    jctrl.set("port", _discover_port);
    jctrl.set("type", "tcp");
    jctrls_list.push_back(jctrl);

    jroot.set("controllers", jctrls_list);

    return jroot;
}

/**
 * Decodes controller information from JSON.
 */
int
Metron::controllers_from_json(const Json &j)
{
    click_chatter("Metron controller requested to update the controllers of this server");

    // A list of controllers is expected
    Json jlist = j.get("controllers");

    for (unsigned short i = 0; i < jlist.size(); i++) {
        String ctrl_ip;
        int    ctrl_port = -1;
        String ctrl_type;

        // Get this controller's information
        Json jctrl = jlist[i];

        // Store the new parameters
        ctrl_ip   = jctrl.get_s("ip");
        ctrl_port = jctrl.get_i("port");
        ctrl_type = jctrl.get_s("type");

        // Incorrect information received
        if (ctrl_ip.empty() || (ctrl_port < 0)) {
            click_chatter(
                "Invalid controller information: IP (%s), Port (%d)",
                ctrl_ip.c_str(), ctrl_port
            );
            return ERROR;
        }

        // Need to re-discover, we got a new controller instance
        if ((ctrl_ip != _discover_ip) || (!_discovered)) {
            _discover_ip   = ctrl_ip;
            _discover_port = ctrl_port;

            click_chatter(
                "Controller instance updated: IP (%s), Port (%d)",
                ctrl_ip.c_str(), ctrl_port
            );

            // Initiate discovery
            _discovered = discover();
            return _discovered;
        } else {
            click_chatter(
                "Controller instance persists: IP (%s), Port (%d)",
                ctrl_ip.c_str(), ctrl_port
            );
        }

        // No support for multiple controller instances
        break;
    }

    return SUCCESS;
}

/**
 * Disassociates a Metron agent from a Metron controller.
 */
int
Metron::controller_delete_from_json(const String &ip)
{
    click_chatter("Metron controller requested to delete the controller of this server");

    // This agent is not associated with a Metron controller at the moment
    if (_discover_ip.empty()) {
        click_chatter("No controller associated with this Metron agent");
        return ERROR;
    }

    // Request to delete a controller must have correct IP
    if ((!ip.empty()) && (ip != _discover_ip)) {
        click_chatter("Metron agent is not associated with a Metron controller on %s", ip.c_str());
        return ERROR;
    }

    click_chatter(
        "Metron controller instance on %s:%d has been deleted",
        _discover_ip.c_str(), _discover_port
    );

    // Reset controller information
    _discovered    = false;
    _discover_ip   = "";
    _discover_port = -1;

    return SUCCESS;
}

/**
 * Metron agent's read handlers.
 */
String
Metron::read_handler(Element *e, void *user_data)
{
    Metron *m = static_cast<Metron *>(e);
    assert(m);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    Json jroot = Json::make_object();

    switch (what) {
        case h_server_discovered: {
            return m->_discovered? "true" : "false";
        }
        case h_server_resources: {
            jroot = m->to_json();
            break;
        }
        case h_server_time: {
            jroot = m->time_to_json();
            break;
        }
        case h_server_stats: {
            jroot = m->system_stats_to_json();
            break;
        }
        case h_nic_queues: {
            jroot = m->nic_queues_report();
            break;
        }
        case h_nic_link_discovery: {
            jroot = m->setup_link_discovery();
            break;
        }
        case h_controllers: {
            jroot = m->controllers_to_json();
            break;
        }
    #if HAVE_FLOW_API
        case h_rules_table_stats: {
            jroot = m->nics_table_stats_to_json();
            break;
        }
    #endif
        default: {
            click_chatter("Unknown read handler: %d", what);
            return "";
        }
    }

    return jroot.unparse(true);
}

/**
 * Metron agent's write handlers.
 */
int
Metron::write_handler(
        const String &data, Element *e, void *user_data, ErrorHandler *errh)
{
    Metron *m = static_cast<Metron *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    switch (what) {
        case h_server_connect: {
            return m->connect(Json::parse(data));
        }
        case h_server_disconnect: {
            return m->disconnect(Json::parse(data));
        }
        case h_nic_ports: {
            return m->nic_port_administrator(Json::parse(data));
        }
        case h_controllers_set: {
            return m->controllers_from_json(Json::parse(data));
        }
        case h_service_chains_delete: {
            ServiceChain *sc = m->find_service_chain_by_id(data);
            if (!sc) {
                return errh->error(
                    "Cannot delete service chain: Unknown service chain ID %s",
                    data.c_str()
                );
            }

            click_chatter("Metron controller requested service chain deletion");

            int ret = m->delete_service_chain(sc, errh);
            if (ret == SUCCESS) {
                errh->message("Deleted service chain with ID: %s", sc->get_id().c_str());
                delete(sc);
            } else {
                errh->error("Failed to delete service chain with ID: %s", sc->get_id().c_str());
            }

            return ret;
        }
        case h_controllers_delete: {
            return m->controller_delete_from_json((const String &) data);
        }
    #if HAVE_FLOW_API
        case h_rules_add_from_file: {
            int delim = data.find_left(' ');
            // Only one argument was given
            if (delim < 0) {
                return errh->error("Handler rules_add_from_file requires 2 arguments <nic> <file-with-rules>");
            }

            // Parse and verify the first argument
            String nic_name = data.substring(0, delim).trim_space_left();

            NIC *nic = m->get_nic_by_name(nic_name);
            if (!nic) {
                return errh->error("Invalid NIC %s", nic_name.c_str());
            }
            FromDPDKDevice *fd = nic->cast();
            if (!fd) {
                return errh->error("Invalid NIC %s", nic_name.c_str());
            }
            portid_t port_id = fd->get_device()->get_port_id();

            // NIC is valid, now parse the second argument
            String filename = data.substring(delim + 1).trim_space_left();

            int32_t installed_rules = FlowRuleManager::get_flow_rule_mgr(port_id)->flow_rules_add_from_file(filename);
            if (installed_rules < 0) {
                return errh->error("Failed to insert NIC flow rules from file %s", filename.c_str());
            }

            return SUCCESS;
        }
        case h_rules_delete: {
            click_chatter("Metron controller requested rule deletion");

            int32_t deleted_rules = ServiceChain::delete_rule_batch_from_json(data, m, errh);
            if (deleted_rules < 0) {
                return ERROR;
            }

            return SUCCESS;
        }
        case h_rules_verify: {
            // Split input arguments
            int delim = data.find_left(' ');
            if (delim < 0) {
                // Only one argument was given
                delim = data.length();
            }

            // Parse and verify the first argument
            String nic_name = data.substring(0, delim).trim_space_left();

            NIC *nic = m->get_nic_by_name(nic_name);
            if (!nic) {
                return errh->error("Invalid NIC %s", nic_name.c_str());
            }
            FromDPDKDevice *fd = nic->cast();
            if (!fd) {
                return errh->error("Invalid NIC %s", nic_name.c_str());
            }
            portid_t port_id = fd->get_device()->get_port_id();

            // NIC is valid, now parse and verify the second argument
            uint32_t rules_present = 0;
            String sec_arg = data.substring(delim + 1).trim_space_left();
            if (sec_arg.empty()) {
                // User did not specify the number of rules, infer it automatically
                rules_present = FlowRuleManager::get_flow_rule_mgr(port_id)->flow_rules_count_explicit();
            } else {
                // User want to enforce the desired number of rules (assuming that he/she knows..)
                rules_present = atoi(sec_arg.c_str());
            }

            click_chatter(
                "Metron controller requested to verify the consistency of NIC %s (port %d) with %u rules present",
                nic_name.c_str(), port_id, rules_present
            );

            FlowRuleManager::get_flow_rule_mgr(port_id)->flow_rule_consistency_check(rules_present);
            return SUCCESS;
        }
        case h_rules_flush: {
            click_chatter("Metron controller requested to flush all NICs");
            return m->flush_nics();
        }
    #endif
        default: {
            errh->error("Unknown write handler: %d", what);
        }
    }

    return ERROR;
}

/**
 * Metron agent's parameter handlers.
 */
int
Metron::param_handler(
        int operation, String &param, Element *e,
        const Handler *h, ErrorHandler *errh)
{
    Metron *m = static_cast<Metron *>(e);

    // Metron agent --> Controller
    if (operation == Handler::f_read) {
        Json jroot = Json::make_object();

        intptr_t what = reinterpret_cast<intptr_t>(h->read_user_data());
        switch (what) {
            case h_service_chains: {
                if (param == "") {
                    Json jscs = Json::make_array();
                    auto begin = m->_scs.begin();
                    while (begin != m->_scs.end()) {
                        jscs.push_back(begin.value()->to_json());
                        begin++;
                    }
                    jroot.set("serviceChains", jscs);
                } else {
                    ServiceChain *sc = m->find_service_chain_by_id(param);
                    if (!sc) {
                        return errh->error(
                            "Cannot report service chain info: Unknown service chain ID %s",
                            param.c_str()
                        );
                    }
                    jroot = sc->to_json();
                }
                break;
            }
            case h_service_chains_stats: {
                if (param == "") {
                    Json jscs = Json::make_array();
                    auto begin = m->_scs.begin();
                    while (begin != m->_scs.end()) {
                        jscs.push_back(begin.value()->stats_to_json(m->get_monitoring_mode()));
                        begin++;
                    }
                    jroot.set("serviceChains", jscs);
                } else {
                    ServiceChain *sc = m->find_service_chain_by_id(param);
                    if (!sc) {
                        return errh->error(
                            "Cannot report statistics: Unknown service chain ID %s",
                            param.c_str()
                        );
                    }
                    jroot = sc->stats_to_json(m->get_monitoring_mode());
                }
                break;
            }
            case h_service_chains_proxy: {
                int pos = param.find_left("/");
                if (pos <= 0) {
                    param = "You must give a service chain ID, then a command";
                    return SUCCESS;
                }
                String ids = param.substring(0, pos);
                ServiceChain *sc = m->find_service_chain_by_id(ids);
                if (!sc) {
                    return errh->error(
                        "Unknown service chain ID: %s",
                        ids.c_str()
                    );
                }
                param = sc->_manager->command(param.substring(pos + 1));
                return SUCCESS;
            }
        #if HAVE_FLOW_API
            case h_rules: {
                if (param == "") {
                    click_chatter("Metron controller requested local NIC rules for all service chains");

                    Json jscs = Json::make_array();

                    auto begin = m->_scs.begin();
                    while (begin != m->_scs.end()) {
                        jscs.push_back(begin.value()->rules_to_json());
                        begin++;
                    }

                    jroot.set("rules", jscs);
                } else {
                    ServiceChain *sc = m->find_service_chain_by_id(param);
                    if (!sc) {
                        return errh->error(
                            "Cannot report NIC rules: Unknown service chain ID %s",
                            param.c_str()
                        );
                    }
                    click_chatter(
                        "Metron controller requested local NIC rules for service chain %s",
                        sc->get_id().c_str()
                    );
                    jroot = sc->rules_to_json();
                }
                break;
            }
        #endif
            default: {
                return errh->error("Invalid read operation in param handler");
            }
        }

        param = jroot.unparse(true);

        return SUCCESS;
    // Controller --> Metron agent
    } else if (operation == Handler::f_write) {
        intptr_t what = reinterpret_cast<intptr_t>(h->write_user_data());
        switch (what) {
            case h_service_chains: {
                Json jroot = Json::parse(param);
                Json jlist = jroot.get("serviceChains");
                if (jlist.size() == 0) {
                    return errh->error("Invalid call to rules' installer with no valid 'serviceChains' array");
                }

                for (auto jsc : jlist) {
                    struct ServiceChain::timing_stats ts;
                    ts.start = Timestamp::now_steady();
                    // Parse
                    ServiceChain *sc = ServiceChain::from_json(jsc.second, m, errh);
                    if (!sc) {
                        return errh->error("Could not instantiate a service chain");
                    }
                    ts.parse = Timestamp::now_steady();

                    String sc_id = sc->get_id();
                    if (m->find_service_chain_by_id(sc_id) != 0) {
                        delete sc;
                        return errh->error(
                            "A service chain with ID %s already exists. Delete it first.", sc_id.c_str()
                        );
                    }

                    // Instantiate
                    int ret = m->instantiate_service_chain(sc, errh);
                    if (ret != 0) {
                        delete sc;
                        return errh->error(
                            "Cannot instantiate service chain with ID %s", sc_id.c_str()
                        );
                    }
                    ts.launch = Timestamp::now_steady();
                    sc->set_timing_stats(ts);
                }

                return SUCCESS;
            }
            case h_service_chains_put: {
                String id = param.substring(0, param.find_left('\n'));
                String changes = param.substring(id.length() + 1);
                ServiceChain *sc = m->find_service_chain_by_id(id);
                if (!sc) {
                    return errh->error(
                        "Cannot reconfigure service chain: Unknown service chain ID %s",
                        id.c_str()
                    );
                }
                int cpu = sc->reconfigure_from_json(Json::parse(changes), m, errh);
                if (cpu < 0)
                    return -1;

                Json ar = Json::make_array();
                ar.push_back(String(cpu));
                param = ar.unparse();
                return SUCCESS;
            }
        #if HAVE_FLOW_API
            case h_rules: {
                Json jroot = Json::parse(param);
                Json jlist = jroot.get("rules");
                for (auto jsc : jlist) {
                    String sc_id = jsc.second.get_s("id");

                    ServiceChain *sc = m->find_service_chain_by_id(sc_id);
                    if (!sc) {
                        return errh->error(
                            "Cannot install NIC rules: Unknown service chain ID %s", sc_id.c_str()
                        );
                    }

                    click_chatter(
                        "Metron controller requested rule installation for service chain %s",
                        sc_id.c_str()
                    );

                    // Parse rules from JSON
                    int32_t installed_rules = sc->rules_from_json(jsc.second, m, errh);
                    if (installed_rules < 0) {
                        delete sc;  // Wrong rules result in a wrong service chain
                        return errh->error(
                            "Cannot install NIC rules for service chain %s: Parse error",
                            sc_id.c_str()
                        );
                    }
                }

                return SUCCESS;
            }
        #endif
            default: {
                return errh->error("Invalid write operation in param handler");
            }

        }
        return ERROR;
    } else {
        return errh->error("Unknown operation in param handler");
    }
}

#if HAVE_FLOW_API
/**
 * Metron agent's rule statistics handlers.
 */
int
Metron::rule_stats_handler(int operation, String &param, Element *e, const Handler *h, ErrorHandler *errh)
{
    Metron *m = static_cast<Metron *>(e);

    if (operation != Handler::f_read) {
        return errh->error("Handler %s is a read handler", h->name().c_str());
    }

    if (param == "") {
        return errh->error("Handler %s requires a NIC instance as input parameter", h->name().c_str());
    }

    NIC *nic = m->get_nic_by_name(param);
    if (!nic) {
        return errh->error("Handler %s requires a valid NIC instance as input parameter", h->name().c_str());
    }
    FromDPDKDevice *fd = nic->cast();
    if (!fd) {
        return errh->error("Handler %s requires a valid NIC instance as input parameter", h->name().c_str());
    }
    portid_t port_id = fd->get_device()->get_port_id();

    float min = std::numeric_limits<float>::max();
    float avg = 0;
    float max = 0;

    intptr_t what = reinterpret_cast<intptr_t>(h->read_user_data());
    switch (what) {
        case h_rule_inst_lat_min:
        case h_rule_inst_lat_avg:
        case h_rule_inst_lat_max: {
            FlowRuleManager::get_flow_rule_mgr(port_id)->min_avg_max(min, avg, max, true, true);
            if ((intptr_t) what == h_rule_inst_lat_min) {
                param = String(min);
            } else if ((intptr_t) what == h_rule_inst_lat_avg) {
                param = String(avg);
            } else {
                param = String(max);
            }
            return SUCCESS;
        }
        case h_rule_inst_rate_min:
        case h_rule_inst_rate_avg:
        case h_rule_inst_rate_max: {
            FlowRuleManager::get_flow_rule_mgr(port_id)->min_avg_max(min, avg, max, true, false);
            if ((intptr_t) what == h_rule_inst_rate_min) {
                param = String(min);
            } else if ((intptr_t) what == h_rule_inst_rate_avg) {
                param = String(avg);
            } else {
                param = String(max);
            }
            return SUCCESS;
        }
        case h_rule_del_lat_min:
        case h_rule_del_lat_avg:
        case h_rule_del_lat_max: {
            FlowRuleManager::get_flow_rule_mgr(port_id)->min_avg_max(min, avg, max, false, true);
            if ((intptr_t) what == h_rule_del_lat_min) {
                param = String(min);
            } else if ((intptr_t) what == h_rule_del_lat_avg) {
                param = String(avg);
            } else {
                param = String(max);
            }
            return SUCCESS;
        }
        case h_rule_del_rate_min:
        case h_rule_del_rate_avg:
        case h_rule_del_rate_max: {
            FlowRuleManager::get_flow_rule_mgr(port_id)->min_avg_max(min, avg, max, false, false);
            if ((intptr_t) what == h_rule_del_rate_min) {
                param = String(min);
            } else if ((intptr_t) what == h_rule_del_rate_avg) {
                param = String(avg);
            } else {
                param = String(max);
            }
            return SUCCESS;
        }
        default: {
            click_chatter("Unknown rule statistics handler: %d", what);
            param = "";
            break;
        }
    }

    return ERROR;
}
#endif

/**
 * Metron's handlers and REST API.
 */
void
Metron::add_handlers()
{
    // Generic server resource handlers
    add_write_handler("server_connect",    write_handler, h_server_connect);
    add_write_handler("server_disconnect", write_handler, h_server_disconnect);
    add_read_handler ("server_discovered", read_handler,  h_server_discovered);
    add_read_handler ("server_time",       read_handler,  h_server_time);
    add_read_handler ("server_resources",  read_handler,  h_server_resources);
    add_read_handler ("server_stats",      read_handler,  h_server_stats);

    // NIC device management handlers
    add_write_handler("nic_ports",     write_handler, h_nic_ports);
    add_read_handler ("nic_queues",    read_handler,  h_nic_queues);
    add_read_handler ("nic_link_disc", read_handler,  h_nic_link_discovery);

    // Controller management handlers
    add_read_handler ("controllers", read_handler,  h_controllers);
    add_write_handler("controllers", write_handler, h_controllers_set);
    add_write_handler("controllers_delete", write_handler, h_controllers_delete);

    // Service chain handlers
    set_handler(
        "service_chains",
        Handler::f_write | Handler::f_read | Handler::f_read_param,
        param_handler, h_service_chains, h_service_chains
    );
    set_handler(
        "service_chains_put",
        Handler::f_write,
        param_handler, h_service_chains_put, h_service_chains_put
    );
    set_handler(
        "service_chains_stats", Handler::f_read | Handler::f_read_param,
        param_handler, h_service_chains_stats
    );
    set_handler(
        "service_chains_proxy", Handler::f_read | Handler::f_read_param,
        param_handler, h_service_chains_proxy
    );
    add_write_handler(
        "service_chains_delete", write_handler, h_service_chains_delete
    );

    // Rule handlers
#if HAVE_FLOW_API
    set_handler(
        "rules", Handler::f_write | Handler::f_read | Handler::f_read_param,
        param_handler, h_rules, h_rules
    );
    add_write_handler("rules_add_from_file", write_handler, h_rules_add_from_file);
    add_read_handler ("rules_table_stats",   read_handler,  h_rules_table_stats);
    add_write_handler("rules_verify",        write_handler, h_rules_verify);
    add_write_handler("rules_delete",        write_handler, h_rules_delete);
    add_write_handler("rules_flush",         write_handler, h_rules_flush);

    set_handler(
        "rule_installation_lat_min", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_inst_lat_min, h_rule_inst_lat_min
    );
    set_handler(
        "rule_installation_lat_avg", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_inst_lat_avg, h_rule_inst_lat_avg
    );
    set_handler(
        "rule_installation_lat_max", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_inst_lat_max, h_rule_inst_lat_max
    );
    set_handler(
        "rule_installation_rate_min", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_inst_rate_min, h_rule_inst_rate_min
    );
    set_handler(
        "rule_installation_rate_avg", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_inst_rate_avg, h_rule_inst_rate_avg
    );
    set_handler(
        "rule_installation_rate_max", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_inst_rate_max, h_rule_inst_rate_max
    );

    set_handler(
        "rule_deletion_lat_min", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_del_lat_min, h_rule_del_lat_min
    );
    set_handler(
        "rule_deletion_lat_avg", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_del_lat_avg, h_rule_del_lat_avg
    );
    set_handler(
        "rule_deletion_lat_max", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_del_lat_max, h_rule_del_lat_max
    );
    set_handler(
        "rule_deletion_rate_min", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_del_rate_min, h_rule_del_rate_min
    );
    set_handler(
        "rule_deletion_rate_avg", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_del_rate_avg, h_rule_del_rate_avg
    );
    set_handler(
        "rule_deletion_rate_max", Handler::f_read | Handler::f_read_param,
        rule_stats_handler, h_rule_del_rate_max, h_rule_del_rate_max
    );
#endif
}

/***************************************
 * RxFilter
 **************************************/
/**
 * RxFilter constructor.
 */
ServiceChain::RxFilter::RxFilter(ServiceChain *sc) : sc(sc)
{}

/**
 * RxFilter destructor.
 */
ServiceChain::RxFilter::~RxFilter()
{
    values.clear();
}

/**
 * Allocates space to store tags for several NICs.
 */
inline void
ServiceChain::RxFilter::allocate_nic_space_for_tags(const int &size)
{
    assert(size > 0);

    // Vector initialization
    if (values.size() == 0) {
        values.resize(size, Vector<String>());
        return;
    }

    // Grow the vector if not large enough
    if (values.size() < size) {
        values.resize(size);
    }
}

/**
 * Allocates space to store tags for a given NIC.
 */
inline void
ServiceChain::RxFilter::allocate_tag_space_for_nic(const int &nic_id, const int &size)
{
    assert(nic_id >= 0);
    assert(size > 0);

    // Vector initialization
    if (values[nic_id].size() == 0) {
        values[nic_id].resize(size, "");
        return;
    }

    // Grow the vector if not large enough
    if (values[nic_id].size() < size) {
        values[nic_id].resize(size);
    }
}

/**
 * Associates a NIC with a tag and a CPU core.
 */
inline void
ServiceChain::RxFilter::set_tag_value(const int &nic_id, const int &cpu_id, const String &value)
{
    assert(nic_id >= 0);
    assert(cpu_id >= 0);
    assert(!value.empty());

    // Grow the vector according to the core index
    allocate_tag_space_for_nic(nic_id, cpu_id + 1);

    values[nic_id][cpu_id] = value;

    click_chatter("Tag %s is mapped to NIC %d and CPU core %d", value.c_str(), nic_id, cpu_id);
}

/**
 * Returns the tag of a NIC associated with a CPU core.
 */
inline String
ServiceChain::RxFilter::get_tag_value(const int &nic_id, const int &cpu_id)
{
    assert(nic_id >= 0);
    assert(cpu_id >= 0);

    return values[nic_id][cpu_id];
}

/**
 * Checks whether a NIC is associated with a CPU core through a tag.
 */
inline bool
ServiceChain::RxFilter::has_tag_value(const int &nic_id, const int &cpu_id)
{
    String value = get_tag_value(nic_id, cpu_id);
    return (value && !value.empty());
}
/**
 * Converts JSON object to Rx filter configuration.
 */
ServiceChain::RxFilter *
ServiceChain::RxFilter::from_json(const Json &j, ServiceChain *sc, ErrorHandler *errh)
{
    ServiceChain::RxFilter *rf = new RxFilter(sc);

    RxFilterType rf_type = rx_filter_type_str_to_enum(j.get_s("method").upper());
    if (rf_type == NONE) {
        errh->error(
            "Unsupported Rx filter mode for service chain: %s\n"
            "Supported Rx filter modes are: %s", sc->id.c_str(),
            supported_types(RX_FILTER_TYPES_STR_ARRAY).c_str()
        );
        return NULL;
    }
    rf->method = rf_type;

    if (sc->get_nics_nb() > 0)
    rf->allocate_nic_space_for_tags(sc->get_nics_nb());
    Json jnic_values = j.get("values");

    int inic = 0;
    for (auto jnic : jnic_values) {
        NIC *nic = sc->get_nic_by_name(jnic.first);
        rf->allocate_tag_space_for_nic(inic, jnic.second.size());
        int j = 0;
        for (auto jtag : jnic.second) {
            const int core_id = j++;
            rf->set_tag_value(inic, core_id, jtag.second.to_s());
        }
        inic++;
    }

    return rf;
}

/**
 * Converts Rx filter configuration to JSON object.
 */
Json
ServiceChain::RxFilter::to_json()
{
    Json j;

    j.set("method", rx_filter_type_enum_to_str(method));

    Json jnic_values = Json::make_object();
    for (unsigned nic_id = 0; nic_id < sc->get_nics_nb(); nic_id++) {
        NIC *nic = sc->get_nic_by_index(nic_id);
        Json jaddrs = Json::make_array();
        for (unsigned j = 0; j < sc->get_max_cpu_nb(); j++) {
            const String tag = get_tag_value(nic_id, j);
            assert(!tag.empty());
            jaddrs.push_back(tag);
        }
        jnic_values.set(nic->get_name(), jaddrs);
    }
    j.set("values", jnic_values);

    return j;
}

/**
 * Prints information about an Rx filter.
 */
void
ServiceChain::RxFilter::print()
{
    if (!sc) {
        return;
    }

    click_chatter("================ Rx Filter ================");
    click_chatter("Service chain ID: %s", sc->get_id().c_str());
    click_chatter("Rx filter mode: %s", rx_filter_type_enum_to_str(method).c_str());
    for (unsigned n = 0; n < values.size(); n++) {
        for (unsigned c = 0; c < values[n].size(); c++) {
            click_chatter("Tag %2d: NIC %2d --> CPU %s", (n+1)*c, n, values[n][c].c_str());
        }
    }
    click_chatter("===========================================");
}

/**
 * Applies an Rx filter configuration to a NIC
 * by allocating space for tags and then populating
 * the desired tag values.
 */
int
ServiceChain::RxFilter::apply(NIC *nic, ErrorHandler *errh)
{
    // Get the NIC index requested by the controller
    int inic = sc->get_nic_index(nic);
    assert(inic >= 0);

    // Allocate the right number of tags
    allocate_nic_space_for_tags(sc->get_nics_nb());
    allocate_tag_space_for_nic(inic, sc->get_max_cpu_nb());

    String method_str = rx_filter_type_enum_to_str(method);
    click_chatter("Rx filters in mode: %s", method_str.upper().c_str());

    if (method == MAC) {
        Json jaddrs = Json::parse(nic->call_rx_read("vf_mac_addr"));

        for (unsigned i = 0; i < sc->get_max_cpu_nb(); i++) {
            int available_pools = atoi(nic->call_rx_read("nb_vf_pools").c_str());
            const int core_id = i;
            if (available_pools <= core_id) {
                return errh->error("Not enough VF pools: %d are available", available_pools);
            }
            set_tag_value(inic, core_id, jaddrs[core_id].to_s());
        }
    } else if ((method == FLOW) || (method == RSS)) {
        // Advertize the available CPU core IDs
        for (unsigned i = 0; i < sc->get_max_cpu_nb(); i++) {
            const int core_id = i;
            set_tag_value(inic, core_id, String(sc->get_cpu_phys_id(core_id)));
        }
    } else if (method == VLAN) {
        return errh->error("VLAN-based dispatching with VMDq is not implemented yet");
    } else {
        return errh->error("Unsupported dispatching method %s", method_str.upper().c_str());
    }

    return SUCCESS;
}

/************************
 * Service Chain
 ************************/
/**
 * ServiceChain constructor.
 */
ServiceChain::ServiceChain(Metron *m)
    : id(), rx_filter(0), config(), config_type(UNKNOWN),
      _metron(m), _manager(0), _nics(), _cpus(), _nic_stats(),
      _initial_cpus_nb(0), _max_cpus_nb(0),
      _total_cpu_load(0), _max_cpu_load(0), _max_cpu_load_index(0),
      _timing_stats(), _as_timing_stats(),
      _autoscale(false), _last_autoscale()
{
    _verbose = m->_verbose;
}

/**
 * ServiceChain destructor.
 */
ServiceChain::~ServiceChain()
{
    if (rx_filter) {
        delete rx_filter;
    }

    if (_manager) {
        delete _manager;
    }

    _cpus.clear();
    _nic_stats.clear();
}

/**
 * Initializes CPU information for a service chain.
 */
void
ServiceChain::initialize_cpus(int initial_cpu_nb, int max_cpu_nb)
{
    _initial_cpus_nb = initial_cpu_nb;
    _max_cpus_nb = max_cpu_nb;
    _autoscale = false;
    _cpus.resize(max_cpu_nb, CPUStats());
}

/**
 * Returns statistics of a CPU core as a JSON object.
 */
Json
ServiceChain::get_cpu_stats(int j)
{
    ServiceChain *sc = this;
    int cpu_id = sc->get_cpu_phys_id(j);
    CPUStats &cpu = sc->get_cpu_info(j);
    Json jcpu = Json::make_object();
    jcpu.set("id", cpu_id);
    jcpu.set("queue", cpu.get_max_nic_queue());
    jcpu.set("load", cpu.get_load());
    jcpu.set("busy", cpu.active_since());

    // Additional per-core statistics in monitoring mode
    if (sc->_metron->_monitoring_mode) {
        LatencyStats lat = sc->get_cpu_info(j).get_latency();
        sc->_metron->add_per_core_monitoring_data(&jcpu, lat);
    }

    return jcpu;
}

/**
 * Decodes service chain information from JSON.
 */
ServiceChain *
ServiceChain::from_json(const Json &j, Metron *m, ErrorHandler *errh)
{
    String new_sc_id = j.get_s("id");

    if ((m->_rx_mode == RSS) && (m->get_service_chains_nb() == 1)) {
        errh->error(
            "Unable to deploy service chain %s: "
            "Metron in RSS mode allows only one service chain per server. "
            "Use RX_MODE flow or mac to run multiple service chains.",
            new_sc_id.c_str()
        );
        return NULL;
    }

    ServiceChain *sc = new ServiceChain(m);
    sc->id = new_sc_id;
    if (sc->id == "") {
        sc->id = String(m->get_service_chains_nb());
    }
    String sc_type_str = j.get_s("configType");
    ScType sc_type = sc_type_str_to_enum(sc_type_str.upper());
    if (sc_type == UNKNOWN) {
        errh->error(
            "Unsupported configuration type for service chain: %s\n"
            "Supported types are: %s", sc->id.c_str(), supported_types(SC_TYPES_STR_ARRAY).c_str()
        );
        delete sc;
        return NULL;
    }
    sc->config_type = sc_type;
    sc->config = j.get_s("config");
    int initial_cpu_nb = j.get_i("cpus");
    int max_cpu_nb = j.get_i("maxCpus");

    if (initial_cpu_nb > max_cpu_nb) {
        errh->error("Max number of CPUs must be greater or equal than the number of used CPUs");
        delete sc;
        return NULL;
    }
    sc->initialize_cpus(initial_cpu_nb, max_cpu_nb);

    sc->_autoscale = false;
    if (!j.get("autoScale", sc->_autoscale)) {
        errh->warning("Autoscale is not present or not boolean. Defaulting to false.");
    }
    /*
    if ((m->_rx_mode == RSS) && sc->_autoscale) {
        errh->error(
            "Unable to deploy service chain %s: "
            "Metron in RSS mode does not allow autoscale.",
            sc->id.c_str()
        );
        return NULL;
    }
    */

    Json jnics = j.get("nics");
    if (!jnics.is_array()) {
        if (jnics.is_string()) {
            errh->warning("NICs should be placed into a JSON array. Assuming you passed one NIC named %s...", jnics.c_str());
            Json ar = Json::make_array();
            ar.push_back(jnics);
            jnics = ar;
        } else {
            errh->error("Invalid JSON format for NICs!");
            delete sc;
            return NULL;
        }
    }
    for (auto jnic : jnics) {
        NIC *nic = m->_nics.findp(jnic.second.as_s());
        if (!nic) {
            errh->error("Unknown NIC: %s", jnic.second.as_s().c_str());
            delete sc;
            return NULL;
        }
        sc->_nics.push_back(nic);
    }

    // Either in mirror mode or a stateful configuration is requested
    if (m->_mirror || (sc->config.find_left("Rewriter(p") > 0)) {
        for (unsigned i = 0; i < sc->_nics.size(); i += 2) {
            if (i + 1 < sc->_nics.size()) {
                sc->_nics[i]->mirror = sc->_nics[i + 1];
            } else {
                sc->_nics[i]->mirror = sc->_nics[i];
            }
            errh->message("NIC %s with mirror NIC %s",
                sc->_nics[i]->get_name().c_str(), sc->_nics[i]->mirror->get_name().c_str());
        }
    }

    sc->_nic_stats.resize(sc->_nics.size() * sc->_max_cpus_nb, NicStat());
    sc->rx_filter = ServiceChain::RxFilter::from_json(j.get("rxFilter"), sc, errh);

    return sc;
}

/**
 * Encodes service chain information to JSON.
 */
Json
ServiceChain::to_json()
{
    Json jsc = Json::make_object();

    jsc.set("id", get_id());
    jsc.set("rxFilter", rx_filter->to_json());
    jsc.set("configType", sc_type_enum_to_str(config_type));
    jsc.set("config", config);
    jsc.set("expandedConfig", generate_configuration(true));
    Json jcpus = Json::make_array();
    for (unsigned i = 0; i < get_max_cpu_nb(); i++) {
        jcpus.push_back(i); // TODO: physical IDs?
    }
    jsc.set("cpus", jcpus);
    jsc.set("maxCpus", get_max_cpu_nb());
    jsc.set("autoScale", _autoscale);
    jsc.set("status", status);
    Json jnics = Json::make_array();
    for (auto n : _nics) {
        jnics.push_back(Json::make_string(n->get_name()));
    }
    jsc.set("nics", jnics);

    return jsc;
}

/**
 * Encodes service chain statistics to JSON.
 */
Json
ServiceChain::stats_to_json(bool monitoring_mode)
{
    Json jsc = Json::make_object();
    jsc.set("id", get_id());

    Json jcpus = Json::make_array();
    for (unsigned j = 0; j < get_max_cpu_nb(); j++) {
        Json jcpu = get_cpu_stats(j);
        jcpus.push_back(jcpu);
    }
    jsc.set("cpus", jcpus);

    // TODO: send memory statistics specific to a service chain (hard)
    jsc.set("memory", _metron->get_system_resources()->get_memory().get_memory_stats().to_json());

    assert(_manager);
    jsc.set("nics", _manager->nic_stats_to_json());

    jsc.set("timingStats", _timing_stats.to_json());
    jsc.set("timingStatsAutoscale", _as_timing_stats.to_json());

    return jsc;
}

#if HAVE_FLOW_API
/**
 * Decodes service chain rules from JSON and installs the rules in the respective NIC.
 *
 * @return the number of installed rules on success, otherwise a negative integer.
 */
int32_t
ServiceChain::rules_from_json(Json j, Metron *m, ErrorHandler *errh)
{
    // No controller
    if (!m->_discovered) {
        return errh->error(
            "Cannot reconfigure service chain %s: Metron agent is not associated with a controller",
            get_id().c_str()
        );
    }

    RxFilterType rx_filter_type = rx_filter_type_str_to_enum(j.get("rxFilter").get_s("method").upper());
    if (rx_filter_type != FLOW) {
        return errh->error(
            "Cannot install rules for service chain %s: "
            "Invalid Rx filter mode %s is sent by the controller.",
            get_id().c_str(),
            rx_filter_type_enum_to_str(rx_filter_type).c_str()
        );
    }

    uint32_t rules_nb = 0;
    uint32_t inserted_rules_nb = 0;

    Json jnics = j.get("nics");
    int inic = 0;
    for (auto jnic : jnics) {
        String nic_name = jnic.second.get_s("name");

        // Get the correct NIC
        NIC *nic = this->get_nic_by_name(nic_name);
        if (!nic) {
            return (int32_t) errh->error(
                "Metron controller attempted to install rules in unknown NIC: %s",
                nic_name.c_str()
            );
        }

        Json jcpus = jnic.second.get("cpus");
        for (auto jcpu : jcpus) {
            int core_id = jcpu.second.get_i("id");
            assert(get_cpu_info(core_id).is_active());

            HashMap<uint32_t, String> rules_map;

            Json jrules = jcpu.second.get("rules");
            for (auto jrule : jrules) {
                uint32_t rule_id = jrule.second.get_i("id");
                String rule = jrule.second.get_s("content");
                rules_nb++;

                // A '\n' must be appended at the end of this rule, if not there
                int eor_pos = rule.find_right('\n');
                if ((eor_pos < 0) || (eor_pos != rule.length() - 1)) {
                    rule += "\n";
                }

                rule = _manager->fix_rule(nic, rule);

                // Store this rule
                rules_map.insert(rule_id, rule);
            }

            int phys_core_id = get_cpu_phys_id(core_id);
            click_chatter("Adding %4d rules for CPU %2d with physical ID %2d", rules_map.size(), core_id, phys_core_id);

            // Update a batch of rules associated with this CPU core ID
            int32_t status = nic->get_flow_rule_mgr()->flow_rules_update(rules_map, true, phys_core_id);
            if (status >= 0) {
                inserted_rules_nb += status;
            }

            if (nic->has_mirror()) {
                click_chatter("Device %s has mirror NIC", nic_name.c_str());
                HashMap<uint32_t, String> mirror_rules_map;

                for (auto jrule : jrules) {
                    uint32_t rule_id = jrule.second.get_i("id");
                    String rule = jrule.second.get_s("content");
                    rules_nb++;

                    // A '\n' must be appended at the end of this rule, if not there
                    int eor_pos = rule.find_right('\n');
                    if ((eor_pos < 0) || (eor_pos != rule.length() - 1)) {
                        rule += "\n";
                    }

                    rule = rule.replace("src", "TOKEN_SRC");
                    rule = rule.replace("dst", "TOKEN_DST");
                    rule = rule.replace("TOKEN_SRC", "dst");
                    rule = rule.replace("TOKEN_DST", "src");
                    rule = _manager->fix_rule(nic->mirror, rule);

                    // Store this rule
                    mirror_rules_map.insert(rule_id, rule);
                }

                click_chatter("Adding %" PRIu32 " MIRROR rules for CPU %d with physical ID %d", mirror_rules_map.size(), core_id, phys_core_id);

                status = nic->mirror->get_flow_rule_mgr()->flow_rules_update(mirror_rules_map, true, phys_core_id);
                if (status >= 0) {
                    inserted_rules_nb += status;
                }
            }

            // Add this tag to the list of tags of this NIC
            if (!this->rx_filter->has_tag_value(inic, core_id)) {
                this->rx_filter->set_tag_value(inic, core_id, String(phys_core_id));
            }
        }

        inic++;
    }

    if (rules_nb > 0) {
        click_chatter(
            "Successfully installed %" PRIu32 "/%" PRIu32  " NIC rules for service chain %s",
            inserted_rules_nb, rules_nb, get_id().c_str()
        );
    }

    return (int32_t) inserted_rules_nb;
}

/**
 * Encodes service chain rules to JSON.
 */
Json
ServiceChain::rules_to_json()
{
    Json jsc = Json::make_object();

    // Service chain ID
    jsc.set("id", get_id());

    // Service chain's Rx filter method
    Json j;
    j.set("method", rx_filter_type_enum_to_str(rx_filter->method));
    jsc.set("rxFilter", j);

    Json jnics_array = Json::make_array();

    // Return an empty array if the Rx filter mode is not set properly
    if (rx_filter->method != FLOW) {
        click_chatter(
            "Cannot report rules for service chain %s: "
            "This service chain is in %s Rx filter mode.",
            get_id().c_str(),
            rx_filter_type_enum_to_str(rx_filter->method).upper().c_str()
        );

        jsc.set("nics", jnics_array);

        return jsc;
    }

    // All NICs
    for (unsigned i = 0; i < get_nics_nb(); i++) {
        NIC *nic = _nics[i];

        Json jnic = Json::make_object();

        jnic.set("name", nic->get_name());

        Json jcpus_array = Json::make_array();

        // One NIC can dispatch to multiple CPU cores
        for (unsigned j = 0; j < get_max_cpu_nb(); j++) {
            // Fetch the rules for this NIC and this CPU core
            HashMap<uint32_t, String> *rules_map = nic->get_flow_rule_cache()->rules_map_by_core_id(j);
            if (!rules_map || rules_map->empty()) {
                continue;
            }

            Json jcpu = Json::make_object();
            jcpu.set("id", j);

            Json jrules = Json::make_array();

            auto begin = rules_map->begin();
            while (begin != rules_map->end()) {
                uint32_t rule_id = begin.key();
                String rule = begin.value();

                Json jrule = Json::make_object();
                jrule.set("id", rule_id);
                jrule.set("content", rule);
                jrules.push_back(jrule);

                begin++;
            }

            jcpu.set("rules", jrules);

            jcpus_array.push_back(jcpu);
        }

        jnic.set("cpus", jcpus_array);

        jnics_array.push_back(jnic);
    }

    jsc.set("nics", jnics_array);

    return jsc;
}

/**
 * Deletes a rule from a NIC.
 */
int
ServiceChain::delete_rule(const uint32_t &rule_id, Metron *m, ErrorHandler *errh)
{
    // No controller
    if (!m->_discovered) {
        errh->error(
            "Cannot delete rule %ld: Metron agent is not associated with a controller",
            rule_id
        );
        return ERROR;
    }

    // Traverse all NICs
    auto it = m->_nics.begin();
    while (it != m->_nics.end()) {
        NIC *nic = &it.value();

        if (!nic->get_flow_rule_cache()->has_rules()) {
            it++;
            continue;
        }

        // Get the internal rule ID from the flow cache
        int32_t int_rule_id = nic->get_flow_rule_cache()->internal_from_global_rule_id(rule_id);

        // This internal rule ID exists, we can proceed with the deletion
        if (int_rule_id >= 0) {
            uint32_t rule_ids[1] = {(uint32_t) int_rule_id};
            return (nic->get_flow_rule_mgr()->flow_rules_delete(rule_ids, 1) == 1)? SUCCESS : ERROR;
        }

        it++;
    }

    return ERROR;
}

/**
 * Deletes a set of rules from a NIC.
 */
int32_t
ServiceChain::delete_rules(const Vector<String> &rules_vec, Metron *m, ErrorHandler *errh)
{
    // No controller
    if (!m->_discovered) {
        return errh->error("Cannot delete rules: Metron agent is not associated with a controller");
    }

    if (rules_vec.empty()) {
        errh->warning("No flow rules to delete");
        return SUCCESS;
    }

    NIC n;
    uint32_t rules_nb = 0;
    uint32_t rules_to_delete = rules_vec.size();
    uint32_t rule_ids[rules_to_delete];

    // Traverse all NICs
    auto it = m->_nics.begin();
    while (it != m->_nics.end()) {
        NIC *nic = &it.value();

        if (!nic->get_flow_rule_cache()->has_rules()) {
            it++;
            continue;
        }

        bool nic_found = false;
        for (uint32_t i = 0; i < rules_vec.size(); i++) {
            uint32_t rule_id = atol(rules_vec[i].c_str());
            int32_t int_rule_id = nic->get_flow_rule_cache()->internal_from_global_rule_id(rule_id);

            // Mapping not found
            if (int_rule_id < 0) {
                continue;
            }

            rule_ids[rules_nb++] = (uint32_t) int_rule_id;
            nic_found = true;
        }

        if (nic_found)
            n = *nic;

        if (rules_nb == rules_to_delete) {
            break;
        }

        it++;
    }

    if (rules_nb == 0) {
        return errh->error("Cannot delete rules: The provided rule IDs are not present in any NIC");
    }

    // Delete the flow rules
    return n.get_flow_rule_mgr()->flow_rules_delete(rule_ids, rules_nb);
}

/**
 * Decodes a string of service chain rule IDs and deletes these
 * rules from the respective NIC.
 * Returns the number of successfully deleted rules on success,
 * otherwise a negative integer.
 */
int32_t
ServiceChain::delete_rule_batch_from_json(String rule_ids, Metron *m, ErrorHandler *errh)
{
    // No controller
    if (!m->_discovered) {
        errh->error(
            "Cannot delete rule batch with IDs %s: Metron agent is not associated with a controller",
            rule_ids.c_str()
        );
        return (int32_t) ERROR;
    }

    const Vector<String> rules_vec = rule_ids.split(',');
    int32_t deleted_rules = ServiceChain::delete_rules(rules_vec, m, errh);
    if (deleted_rules < 0) {
        return ERROR;
    }

    click_chatter("Successfully deleted %d/%d NIC rules", deleted_rules, rules_vec.size());

    return deleted_rules;
}
#endif

/**
 * Encodes service chain's timing statistics to JSON.
 */
Json
ServiceChain::timing_stats::to_json()
{
    Json j = Json::make_object();
    j.set("unit", "ns");
    j.set("parseTime",  (parse - start).nsecval());
    j.set("launchTime", (launch - parse).nsecval());
    j.set("deployTime", (launch - start).nsecval());
    return j;
}

/**
 * Encodes service chain's timing statistics related to autoscale to JSON.
 */
Json
ServiceChain::autoscale_timing_stats::to_json()
{
    Json j = Json::make_object();
    j.set("unit", "ns");
    j.set("autoScaleTime", (autoscale_end - autoscale_start).nsecval());
    return j;
}

/**
 * Decodes service chain's reconfiguration from JSON
 * and applies this reconfiguration.
 */
int
ServiceChain::reconfigure_from_json(Json j, Metron *m, ErrorHandler *errh)
{
    // No controller
    if (!m->_discovered) {
        click_chatter(
            "Cannot reconfigure service chain: Metron agent is not associated with a controller"
        );
        return ERROR;
    }

    for (auto jfield : j) {
        if (jfield.first == "cpus") {
            Bitvector old_map = active_cpus();
            Bitvector new_map(get_max_cpu_nb(), false);
            HashTable<int, Bitvector> migration;
            if (jfield.second.is_array()) {
                for (uint32_t i = 0; i < jfield.second.size(); i++) {
                    int cpuId;
                    if (jfield.second[i].is_object()) {
                        Json jcpustate = jfield.second[i];
                        Json::object_iterator it = jcpustate.obegin();
                        cpuId = atoi(it.key().c_str());
                        if (cpuId == 0) {
                            return errh->error("State migration cpu map have indexes starting at 1 !");
                        }
                        Json s = it.value();
                        Bitvector state(get_max_cpu_nb());
                        for (uint32_t j = 0; j < s.size(); j++) {
                            state[s[j].to_i()] = true;
                        }
                        if (cpuId < 0) {
                            cpuId = -cpuId - 1;
                            migration[cpuId] = state;
                            new_map[cpuId] = false;
                        } else {
                            cpuId = cpuId - 1;
                            new_map[cpuId] = true;
                            migration[cpuId] = state;
                        }
                        click_chatter("CPU %d is migrating with %s", state.unparse().c_str());
                    } else {
                        cpuId = jfield.second[i].to_i();
                        new_map[cpuId] = true;
                    }

                    if (abs(cpuId) > get_max_cpu_nb()) {
                        return errh->error("Number of used CPUs must be less or equal than the maximum number of CPUs!");
                    }

                }
            } else {
                click_chatter("Unknown CPU map %s!", jfield.second.c_str());
            }
            int ret;
            String response = "";
            bool did_scale = false;

            if (_metron->_rx_mode == RSS) {
                for (unsigned i = 0; i < new_map.size(); i++) {
                    if (!new_map[i] && i < new_map.weight()) {
                        return errh->error("RSS must allocate CPUs in order!");
                    }
                }
            }

            for (unsigned new_cpu_id = 0; new_cpu_id < get_max_cpu_nb(); new_cpu_id++) {
                if (old_map[new_cpu_id] == new_map[new_cpu_id]) {
                    continue;
                }

                did_scale = true;

                get_cpu_info(new_cpu_id).set_active(new_map[new_cpu_id]);
                // Scale up
                if (new_map[new_cpu_id]) {
                    ret = _manager->activate_core(new_cpu_id, errh);
                // Scale down
                } else {
                    ret = _manager->deactivate_core(new_cpu_id, errh);
                }
            }

            if (did_scale) {
                m->call_scale(this, "scale");
            }

            click_chatter("Number of active CPUs is now: %d", get_active_cpu_nb());

            return 0;
        } else {
            return errh->error(
                "Unsupported reconfiguration option: %s",
                jfield.first.c_str()
            );
        }
    }

    return SUCCESS;
}

/**
 * Scales a service chain in or out based on local perception of the
 * service chain's state.
 * This function is orthogonal to the scaling performed by the controller.
 */
void
ServiceChain::do_autoscale(int n_cpu_change)
{
    ErrorHandler *errh = ErrorHandler::default_handler();
    if ((Timestamp::now() - _last_autoscale).msecval() < AUTOSCALE_WINDOW) {
        return;
    }

    _last_autoscale = Timestamp::now();
    if (n_cpu_change == 0) {
        return;
    }

    int nnew = get_active_cpu_nb() + n_cpu_change;
    if ((nnew <= 0) || (nnew > get_max_cpu_nb())) {
        return;
    }

    // Measure the time it takes to perform an autoscale
    struct ServiceChain::autoscale_timing_stats ts;
    ts.autoscale_start = Timestamp::now_steady();

    int last_idx = get_active_cpu_nb() - 1;
    for (unsigned i = 0; i < abs(n_cpu_change); i++) {
        get_cpu_info(last_idx + (n_cpu_change > 0 ? i : -1)).set_active(n_cpu_change > 0);
    }
    click_chatter(
        "Autoscale: Service chain %s uses %d CPU(s)",
        this->get_id().c_str(), get_active_cpu_nb()
    );

    assert(_manager);
    _manager->do_autoscale(errh);

    // Measure again
    ts.autoscale_end = Timestamp::now_steady();
    click_chatter(
        "Autoscale: Duration %d nsec",
        (ts.autoscale_end - ts.autoscale_start).nsecval()
    );
    this->set_autoscale_timing_stats(ts);
}

/**
 * Returns a name for the FromDPDKDevice element of a Metron slave process.
 */
String
ServiceChain::generate_configuration_slave_fd_name(
    const int &nic_index, const int &cpu_index, const String &type)
{
    return "slave" + type + String(nic_index) + "C" + String(cpu_index);
}

/**
 * Generates the software configuration for a given service chain
 * as received by the controller.
 */
String
ServiceChain::generate_configuration(bool add_extra)
{
    String new_conf = "elementclass MetronSlave {\n" + config + "\n};\n\n";
    if (_autoscale) {
        new_conf += "slave :: {\n";

        new_conf += "rrs :: RoundRobinSwitch(MAX " + String(get_active_cpu_nb()) + ");\n";
        new_conf += "ps :: PaintSwitch();\n\n";

        for (unsigned i = 0 ; i < get_max_cpu_nb(); i++) {
            new_conf += "rrs[" + String(i) + "] -> slavep" + String(i) +
                       " :: Pipeliner(CAPACITY 8, BLOCKING false) -> "
                       "[0]ps; StaticThreadSched(slavep" +
                       String(i) + " " + String(get_cpu_phys_id(i)) + ");\n";
        }
        new_conf += "\n";

        for (unsigned i = 0; i < get_nics_nb(); i++) {
            String is = String(i);
            new_conf += "input[" + is + "] -> Paint(" + is + ") -> rrs;\n";
        }
        new_conf += "\n";

        new_conf += "ps => [0-" + String(get_nics_nb()-1) +
                   "]real_slave :: MetronSlave() => [0-" +
                   String(get_nics_nb()-1) + "]output }\n\n";
    } else {
        new_conf += "slave :: MetronSlave();\n\n";
    }

    if (_metron->_monitoring_mode && get_nics_nb() > 0) {
        new_conf+= "monitoring_lat :: TimestampAccumMP();\n\n";
    }

    // Common parameters
    String rx_conf = "BURST 32, NUMA false, VERBOSE 99, ";

    // NICs require an additional parameter, if in FLOW mode
    if (get_rx_mode() == FLOW) {
        rx_conf += "MODE flow, ";
    }

    for (unsigned i = 0; i < get_nics_nb(); i++) {
        String is = String(i);
        NIC *nic = get_nic_by_index(i);
        if (_metron->_rx_mode == RSS) {
            nic->call_rx_write("max_rss", String(get_active_cpu_nb()));
        }

        for (unsigned j = 0; j < get_max_cpu_nb(); j++) {
            assert(get_cpu_info(j).is_assigned());
            String js = String(j);
            String active = (j < get_cpu_info(j).is_active() ? "1":"0");
            int phys_cpu_id = get_cpu_phys_id(j);
            int queue_no = rx_filter->phys_cpu_to_queue(nic, phys_cpu_id);
            String ename = generate_configuration_slave_fd_name(i, j);
            new_conf += ename + " :: " + nic->get_element()->class_name() +
                "(" + nic->get_device_address() + ", QUEUE " + String(queue_no) +
                ", N_QUEUES 1, MAXTHREADS 1, ";
            new_conf += rx_conf;
            new_conf += "ACTIVE " + active + ");\n";
            new_conf += "StaticThreadSched(" + ename + " " + String(phys_cpu_id) + ");\n";
            new_conf += ename + " " + " -> ";
            if (_metron->_monitoring_mode) {
                new_conf += "SetTimestamp -> monitoring_th_" + is + "_" + js + " :: AverageCounter -> ";
            }
            new_conf += "[" + is + "]slave;\n";
        }
        new_conf += "\n";

        if (get_max_cpu_nb() == 1) {
            assert(get_cpu_info(0).is_assigned());
            int phys_cpu_id = get_cpu_phys_id(0);
            int queue_no = rx_filter->phys_cpu_to_queue(nic, phys_cpu_id);
            new_conf += "slaveTD" + is + " :: Null -> " + _metron->_slave_td_args + "ToDPDKDevice(" + nic->get_device_address() + ", QUEUE " + String(queue_no) + ", VERBOSE 99);\n";
        } else {
            new_conf += "slaveTD" + is + " :: ExactCPUSwitch();\n";
            for (unsigned j = 0; j < get_max_cpu_nb(); j++) {
                String js = String(j);
                assert(get_cpu_info(j).is_assigned());
                int phys_cpu_id = get_cpu_phys_id(j);
                String ename = generate_configuration_slave_fd_name(i, j, "TD");
                int queue_no = rx_filter->phys_cpu_to_queue(nic, phys_cpu_id);
                new_conf += ename + " :: ToDPDKDevice(" + nic->get_device_address() + ", QUEUE " + String(queue_no) + ", VERBOSE 99, MAXQUEUES 1);";

                new_conf += "slaveTD" + is + "["+js+"] -> "+ _metron->_slave_td_args + " " + ename + ";\n";
            }
        }
        new_conf += "slave[" + is + "] -> " + (_metron->_monitoring_mode ? "[" + is + "]monitoring_lat[" + is + "] -> " : "") + "  slaveTD" + is + ";\n\n";
    }

    if (add_extra)
        new_conf += _metron->_slave_extra;

    return new_conf;
}

/**
 * Returns a bit map with the CPU core assignment of a service chain.
 */
Bitvector
ServiceChain::active_cpus()
{
    Bitvector b;
    b.resize(get_max_cpu_nb());
    for (unsigned i = 0; i < b.size(); i++) {
        b[i] = _cpus[i].is_active();
    }
    return b;
}

/**
 * Returns a bit map with the CPU core assignment of a service chain.
 */
Bitvector
ServiceChain::assigned_phys_cpus()
{
    Bitvector b;
    b.resize(click_max_cpu_ids());
    for (unsigned i = 0; i < get_max_cpu_nb(); i++) {
        int pid = _cpus[i].get_physical_id();
        if (pid >= 0) {
            b[pid] = true;
        }
    }
    return b;
}

void
ServiceChain::print()
{
    click_chatter("======================== Service chain ========================");
    click_chatter(" Service chain ID: %s", id.c_str());
    click_chatter("      Config type: %s", sc_type_enum_to_str(config_type).c_str());
    click_chatter("           Config: \n%s\n", config.c_str());
    click_chatter("  Expanded Config: \n%s\n", generate_configuration(true).c_str());
    rx_filter->print();
    click_chatter("\n");
    for (unsigned j = 0; j < get_max_cpu_nb(); j++) {
        int cpu_id = get_cpu_phys_id(j);
        CPUStats &cpu = get_cpu_info(j);
        click_chatter("CPU ID: %d", cpu_id);
        click_chatter(" Queue: %d", cpu.get_max_nic_queue());
        click_chatter("  Load: %f", cpu.get_load());
        click_chatter("Active: %s", cpu.is_active() ? "true" : "false");
        click_chatter("  Busy since: %d", cpu.active_since());
    }
    click_chatter("===============================================================");
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk17 Json)

EXPORT_ELEMENT(Metron)
ELEMENT_MT_SAFE(Metron)
