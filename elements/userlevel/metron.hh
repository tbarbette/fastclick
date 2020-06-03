// -*- mode: c++; c-basic-offset: 4 -*-

#ifndef CLICK_METRON_HH
#define CLICK_METRON_HH

#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/task.hh>
#include <click/notifier.hh>
#include <click/hashmap.hh>
#include <click/dpdkdevice.hh>
#include <click/handlercall.hh>

#include "fromdpdkdevice.hh"
#include "../json/json.hh"

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
 #include <click/flowdispatcher.hh>
#endif

class ServiceChainManager;

/**
 * CPU Vendors.
 */
#define CPU_VENDORS \
    cpuvendor(GENUINE_INTEL), \
    cpuvendor(AUTHENTIC_AMD), \
    cpuvendor(UNKNOWN_VENDOR)

#define cpuvendor(x) x

typedef enum { CPU_VENDORS } CpuVendor;

#undef cpuvendor
#define cpuvendor(x) #x

/**
 * Types of CPU caches:
 * |-> Data cache
 * |-> CPU instructions' cache
 * |-> Unified (both data and instructions)
 */
#define CPU_CACHE_TYPES \
    cpucachetype(DATA), \
    cpucachetype(INSTRUCTION), \
    cpucachetype(UNIFIED), \
    cpucachetype(UNKNOWN_CACHE_TYPE)

#define cpucachetype(x) x

typedef enum { CPU_CACHE_TYPES } CpuCacheType;

#undef cpucachetype
#define cpucachetype(x) #x

/**
 * Levels of CPU caches:
 * |-> Register
 * |-> L1 cache
 * |-> L2 cache
 * |-> L3 cache
 * |-> L4 cache
 * |-> Last-level cache (LLC)
 */
#define CPU_CACHE_LEVELS \
    cpucachelevel(REGISTER), \
    cpucachelevel(L1), \
    cpucachelevel(L2), \
    cpucachelevel(L3), \
    cpucachelevel(L4), \
    cpucachelevel(LLC), \
    cpucachelevel(UNKNOWN_LEVEL)

#define cpucachelevel(x) x

typedef enum { CPU_CACHE_LEVELS } CpuCacheLevel;

#undef cpucachelevel
#define cpucachelevel(x) #x

/**
 * Replacement policies of CPU caches:
 * |-> Direct-Mapped"
 * |-> Fully-Associative
 * |-> Set-Associative
 */
#define CPU_CACHE_POLICIES \
    cpucachepolicy(DIRECT_MAPPED), \
    cpucachepolicy(FULLY_ASSOCIATIVE), \
    cpucachepolicy(SET_ASSOCIATIVE), \
    cpucachepolicy(UNKNOWN_POLICY)

#define cpucachepolicy(x) x

typedef enum { CPU_CACHE_POLICIES } CpuCachePolicy;

#undef cpucachepolicy
#define cpucachepolicy(x) #x

/**
 * Types of main memory modules:
 * |-> Static Random Access Memory (SRAM)
 * |-> Dynamic Random Access Memory (DRAM)
 * |-> Synchronous DRAM (SDRAM)
 * |-> Double data-rate synchronous DRAM (DDR-SDRAM)
 *     |--> 2nd Generation DDR (DDR2)
 *     |--> 3rd Generation DDR (DDR3)
 *     |--> 4th Generation DDR (DDR4)
 *     |--> 5th Generation DDR (DDR5) (coming soon)
 * |-> Cached DRAM (CDRAM)
 * |-> Embedded DRAM (EDRAM)
 * |-> Synchronous Graphics RAM (SGRAM)
 * |-> Video DRAM (VRAM)
 *
 * |-> Programmable read-only memory (PROM)
 * |-> Erasable Programmable read only memory (EPROM)
 * |-> Flash Erasable Programmable Read Only Memory (FEPROM)
 * |-> Electrically erasable programmable read only memory (EEPROM)
 */
#define MEMORY_TYPES \
    memorytype(SRAM), \
    memorytype(DRAM), \
    memorytype(SDRAM), \
    memorytype(DDR), \
    memorytype(DDR2), \
    memorytype(DDR3), \
    memorytype(DDR4), \
    memorytype(DDR5), \
    memorytype(CDRAM), \
    memorytype(EDRAM), \
    memorytype(SGRAM), \
    memorytype(VRAM), \
    memorytype(PROM), \
    memorytype(EPROM), \
    memorytype(FEPROM), \
    memorytype(EEPROM), \
    memorytype(UNKNOWN_MEMORY_TYPE)

#define memorytype(x) x

typedef enum { MEMORY_TYPES } MemoryType;

#undef memorytype
#define memorytype(x) #x

/**
 * The service chain types supported by Metron:
 * |-> Click-based
 * |-> Standalone (a standalone blackbox NF)
 * |-> Mixed (Click NFs followed by a blackbox NF)
 */
#define SC_CONF_TYPES \
    sctype(UNKNOWN), \
    sctype(CLICK),   \
    sctype(MIXED),   \
    sctype(STANDALONE)

#define sctype(x) x

typedef enum { SC_CONF_TYPES } ScType;

#undef sctype
#define sctype(x) #x

/**
 * The Rx filter types supported by Metron:
 * |->  MAC-based using VMDq
 * |-> VLAN-based using VMDq
 * |-> Flow-based using Flow API
 * |-> Hash-based using RSS (default FastClick)
 */
#define RX_FILTER_TYPES \
    rxfiltertype(NONE), \
    rxfiltertype(MAC), \
    rxfiltertype(VLAN), \
    rxfiltertype(FLOW), \
    rxfiltertype(RSS)

#define rxfiltertype(x) x

typedef enum { RX_FILTER_TYPES } RxFilterType;

#undef rxfiltertype
#define rxfiltertype(x) #x

CLICK_DECLS

/*
=c

Metron(
    [ID, ] NIC, RX_MODE,
    AGENT_IP, AGENT_PORT,
    DISCOVER_IP, DISCOVER_PORT,
    DISCOVER_PATH, DISCOVER_USER,
    DISCOVER_PASSWORD, PIN_TO_CORE,
    MONITORING, FAIL, LOAD_TIMER,
    ON_SCALE, NODISCOVERY,
    MIRROR, VERBOSE
    [, SLAVE_DPDK_ARGS, SLAVE_ARGS,
    SLAVE_EXTRA, SLAVE_TD_EXTRA]
)

=s userlevel

Metron data plane agent for high performance service chaining.

=d

Receives and executes instructions from a remote Metron controller instance.
These instructions are related to the management of high performance NFV
service chains, including flow dispatching to specific CPU cores,
instantiation of dedicated slave processes for flow processing, and NIC
offloading, monitoring, and management operations.
The Metron agent also reports monitoring statistics to the controller.

Keyword arguments are:

=over 19

=item ID

String. The ID of this Metron data plane agent.
If no ID is given, the agent generates a random ID.

=item NIC

String. Instance of a FromDPDKDevice element.
Multiple instances can be supplied by invoking
NIC <instance i> i times.

=item RX_MODE

String. The mode of the underlying FromDPDKDevice elements.
Three modes are supported as follows:
1) FLOW: The NIC utilizes DPDK's Flow API to classify and
dispatch input flows to the system's CPU cores. FromDPDKDevice
elements must be configured with MODE flow.
The Metron controller sends the rules to be installed in the NIC.
There rules ressemble a typical match-action API, where one of the
actions is to dispatch the matched flow to a certain NIC hardware queue,
where a CPU core is waiting for additional processing.
2) MAC: The NIC utilizes the destination MAC address of incoming
packets for dispatching to the correct CPU core, using Virtual
Machine Device queues (VMDq). FromDPDKDevice elements must be
configured with MODE vmdq.
This mode requires an additional network element (e.g., a programmable
switch), prior to the Metron server, to set the destination MAC address
of each packet according to the values advertized by the Metron agent.
If this is not done, incoming traffic will never be dispatched to
a CPU core, as the destination MAC address will likely be wrong.
3) RSS: The NIC utilizes its hash-based Receive-Side Scaling (RSS)
function to distribute incoming traffic to the system's CPU cores.
FromDPDKDevice elements must be configured with MODE rss. This is
the standard FastClick mode, which does not reap the benefits of
Metron, but it is supported for ccompatibility reasons.
Default RX_MODE is FLOW.

=item AGENT_IP

String. The IP address of this Metron data plane agent.
Used to communicate with the Metron controller.

=item AGENT_PORT

Integer. The port of this Metron data plane agent.
Used to communicate with the Metron controller.
The communication is web-based, thus the default port is
usually 80.

=item DISCOVER_IP

String. The IP address of the remote Metron controller instance.

=item DISCOVER_PORT

Integer. The port of the remote Metron controller instance.
Because the Metron controller is based on the ONOS SDN controller,
this port defaults to 8181.

=item DISCOVER_PATH

String. The web resource path where the Metron controller expects
requests. Defaults to '/onos/v1/network/configuration/'.

=item DISCOVER_USER

String. The username to access Metron controller's web services.
Defaults to 'onos'.

=item DISCOVER_PASSWORD

String. The password to access Metron controller's web services.
Defaults to 'rocks'.

=item PIN_TO_CORE

Integer. The CPU core to pin the Metron data plane agent. Defaults to 0.

=item MONITORING

Boolean. If true, the Metron data plane agent monitors throughput and
latency statistics per-core, which are sent to the controller.
Defaults to false.

=item FAIL

Boolean. If true, the Metron agent in allowed to fail.
Defaults to false.

=item LOAD_TIMER

Integer. Specifies the frequency (in milliseconds) that the Metron agent
is rescheduled. Defaults to 1000 ms.

=item ON_SCALE

Boolean. If true, a handler for scaling events is setup. Defaults to false.

=item NODISCOVERY

Boolean. If true, Metron controller should initiate Metron agents' discovery.
Defaults to false.

=item MIRROR

Boolean. If true, the Metron agent mirrors the rules that correspond to
forward traffic on a NIC where the backward traffic is expected.
Defaults to false.

=item VERBOSE

Boolean. If true, more detailed messages about Metron are printed.
Defaults to false.

=item SLAVE_ARGS

String. DPDK arguments to pass to the primary DPDK process, which is the
Metron data plane agent.

=item SLAVE_DPDK_ARGS

String. DPDK arguments to pass to the deployed service chain instances,
which typically are secondary DPDK processes. For example, the following
arguments could be passed: '-b 03:00.0' if you want a certain NIC to be
blacklisted by a service chain.

=item SLAVE_EXTRA

String. Additional arguments for DPDK slave processes.
Default is no additional argument.

=item SLAVE_TD_EXTRA

String. Additional arguments for Tx elements of DPDK slave processes.
Default is no additional argument.

=back

=h server_connect write

Schedules the discovery timer to connect to the controller.

=h server_disconnect write

Un-schedules the discovery timer to disconnect from the controller.

=h server_discovered read-only

Returns whether the Metron agent is associated with a controller or not.

=h server_time read-only

Returns a JSON object with the local time measured by the Metron agent.

=h server_resources read-only

Returns a JSON object with information about the Metron agent.

=h server_stats read-only

Returns a JSON object with global statistics about the Metron agent.


=h nic_ports write

Controller-driven port administration commands (i.e., enable/disable).

=h nic_queues read-only

Returns a JSON object with information about NIC(s) queues.

=h nic_link_disc read-only

Returns a JSON object with link discovery information.


=h controllers read/write

Returns or sets the controller instance associated with this Metron agent.

=h controllers_delete write-only

Disassociates this Metron agent from a Metron controller instance.


=h service_chains read/write

Returns the currently deployed service chains or instantiates a set of new
service chains encoded as a JSON object.

=h service_chains_put write-only

Reconfigures a set of already deployed service chains encoded as a JSON object.

=h service_chains_proxy read-only

Proxies controller commands to service chains.

=h service_chains_stats read-only

Returns a JSON object with either all service chain-level statistics of the
deployed service chains or statistics only for a desired service chain.

=h service_chains_delete write-only

Tears down a deployed service chain.


=h rules read/write

Returns or sets the rules associated with either all deployed service chains or a specific
service chain.

=h rules_add_from_file write-only

Installs a set of NIC rules from file.

=h rules_table_stats read-only

Returns a JSON object with table statistics from the Metron agent's NICs.

=h rules_verify write-only

Verifies the consistency between the rules in the FlowCache (software) and the rules
installed into the NIC (hardware). The user must know the correct number of rules in
the NIC.
Usage example (assuming that NIC fd0 has 150 rules): rules_verify fd0 150

=h rules_delete write-only

Removes a given list of comma-separated rules associated with (a) service chain(s).
This command is issued by an associated Metron controller.
To manually delete rules from a given NIC, use the FromDPDKDevice rules_del handler.

=h rules_flush write-only

Flushes the rules from all Metron NICs.

=h rule_installation_lat_min read-only

Returns the minimum latency (ms) to install rules in the input DPDK-based NIC.

=h rule_installation_lat_avg read-only

Returns the average latency (ms) to install rules in the input DPDK-based NIC.

=h rule_installation_lat_max read-only

Returns the maximum latency (ms) to install rules in the input DPDK-based NIC.

=h rule_installation_rate_min read-only

Returns the minimum rate to install rules in the input DPDK-based NIC.

=h rule_installation_rate_avg read-only

Returns the average rate to install rules in the input DPDK-based NIC.

=h rule_installation_rate_max read-only

Returns the maximum rate to install rules in the input DPDK-based NIC.


=h rule_deletion_lat_min read-only

Returns the minimum latency (ms) to remove rules from the input DPDK-based NIC.

=h rule_deletion_lat_avg read-only

Returns the average latency (ms) to remove rules from the input DPDK-based NIC.

=h rule_deletion_lat_max read-only

Returns the maximum latency (ms) to remove rules from the input DPDK-based NIC.

=h rule_deletion_rate_min read-only

Returns the minimum rate to remove rules from the input DPDK-based NIC.

=h rule_deletion_rate_avg read-only

Returns the average rate to remove rules from the input DPDK-based NIC.

=h rule_deletion_rate_max read-only

Returns the maximum rate to remove rules from the input DPDK-based NIC.
*/

// Return status
const int ERROR = -1;
const int SUCCESS = 0;

class PlatformInfo {
    public:
        PlatformInfo();
        PlatformInfo(String hw, String sw, String serial, String chassis);
        ~PlatformInfo();

        inline void set_hw_info(const String &hw_info) { _hw_info = hw_info; };
        inline void set_sw_info(const String &sw_info) { _sw_info = sw_info; };
        inline void set_serial_number(const String &serial_nb) { _serial_nb = serial_nb; };
        inline void set_chassis_id(const String &chassis_id) { _chassis_id = chassis_id; };

        inline String get_hw_info() { return _hw_info; };
        inline String get_sw_info() { return _sw_info; };
        inline String get_serial_number() { return _serial_nb; };
        inline String get_chassis_id() { return _chassis_id; };

        void print();

    private:
        String _hw_info;
        String _sw_info;
        String _serial_nb;
        String _chassis_id;
};

class LatencyStats {
    public:
        LatencyStats();
        ~LatencyStats();

        inline void set_avg_throughput(uint64_t avg_throughput) { _avg_throughput = avg_throughput; };
        inline void set_min_latency(uint64_t min_latency) { _min_latency = min_latency; };
        inline void set_avg_latency(uint64_t avg_latency) { _avg_latency = avg_latency; };
        inline void set_max_latency(uint64_t max_latency) { _max_latency = max_latency; };

        inline uint64_t get_avg_throughput() { return _avg_throughput; };
        inline uint64_t get_min_latency() { return _min_latency; };
        inline uint64_t get_avg_latency() { return _avg_latency; };
        inline uint64_t get_max_latency() { return _max_latency; };

        void print();

    private:
        uint64_t _avg_throughput;
        uint64_t _min_latency;
        uint64_t _avg_latency;
        uint64_t _max_latency;
};

class CPUStats {
    public:
        CPUStats();
        CPUStats &operator=(CPUStats &r);
        ~CPUStats();

        inline void set_physical_id(int physical_id) { _physical_id = physical_id; };
        inline void set_load(float load) { _load = load; };
        inline void set_max_nic_queue(int max_nic_queue) { _max_nic_queue = max_nic_queue; };
        inline void set_active(bool active) {
            _active = active;
            if (active)
                _active_time = Timestamp::now_steady();
        };
        inline void set_latency(LatencyStats &latency) { _latency = latency; };

        inline int get_physical_id() { return _physical_id; };
        inline bool is_assigned() { return _physical_id >= 0; };
        inline float get_load() { return _load; };
        inline int get_max_nic_queue() { return _max_nic_queue; };
        inline bool is_active() { return _active; };
        inline Timestamp &get_active_time() { return _active_time; };
        inline LatencyStats &get_latency() { return _latency; };

        int active_since();
        void print();

    private:
        int _physical_id;        // CPU core's physical ID
        float _load;             // CPU load in [0, 100]
        int _max_nic_queue;      // Maximum assigned NIC queue index
        bool _active;            // CPU activity status
        Timestamp _active_time;  // Amount of time being active
        LatencyStats _latency;   // Latency information
};

class CPU {
    public:
        CPU(CpuVendor vendor, int phy_id, int log_id, int socket, long frequency);
        ~CPU();

        inline CpuVendor get_vendor() { return _vendor; };
        inline int get_physical_id() { return _physical_id; };
        inline int get_logical_id() {return _logical_id; };
        inline int get_socket() { return _socket; };
        inline long get_frequency() { return _frequency; };
        inline CPUStats &get_stats() { return _stats; };

        inline void set_stats(CPUStats &s) { _stats = s; };

        void print();
        Json to_json();

        static const int MEGA_HZ = 1000000;
        static const int MAX_CPU_CORE_NB = 512;

    private:
        CpuVendor _vendor;       // CPU vendor
        int _physical_id;        // Physical core ID
        int _logical_id;         // Logical core ID
        int _socket;             // CPU socket ID
        long _frequency;         // Clock frequency in MHz
        CPUStats _stats;         // Run-time CPU statistics
};

class CpuCacheId {
    public:
        CpuCacheId();
        CpuCacheId(CpuCacheLevel level, CpuCacheType type);
        CpuCacheId(String level_str, String type_str);
        ~CpuCacheId();

        inline CpuCacheLevel get_level() { return _level; };
        inline CpuCacheType get_type() { return _type; };

    private:
        CpuCacheLevel _level;
        CpuCacheType _type;
};

class CpuCache {
    public:
        CpuCache();
        CpuCache(CpuCacheLevel level, CpuCacheType type,
            CpuCachePolicy policy, CpuVendor vendor,
            long capacity, int sets, int ways, int line_length, bool shared);
        CpuCache(String level_str, String type_str, String policy_str, String vendor_str,
            long capacity, int sets, int ways, int line_length, bool shared);
        ~CpuCache();

        inline CpuCacheId *get_cache_id() { return _cache_id; };
        inline CpuCachePolicy get_policy() { return _policy; };
        inline CpuVendor get_vendor() { return _vendor; };
        inline long get_capacity() { return _capacity; };
        inline int get_sets() { return _sets; };
        inline int get_ways() { return _ways; };
        inline int get_line_length() { return _line_length; };
        inline bool is_shared() { return _shared; };

        void print();
        Json to_json();

        static CpuCacheLevel level_from_integer(const int &level);
        static int level_to_integer(const CpuCacheLevel &level);
        static CpuCacheType type_from_string(const String &type);
        static CpuCachePolicy policy_from_ways(const int &ways, const int &sets);

    private:
        CpuCacheId *_cache_id;
        CpuCachePolicy _policy;
        CpuVendor _vendor;
        long _capacity;
        int _sets;
        int _ways;
        int _line_length;
        bool _shared;
};

class CpuCacheHierarchy {
    public:
        CpuCacheHierarchy(CpuVendor vendor, int sockets_nb, int cores_nb);
        CpuCacheHierarchy(String vendor_str, int sockets_nb, int cores_nb);
        ~CpuCacheHierarchy();

        inline CpuVendor get_vendor() { return _vendor; };
        inline int get_sockets_nb() { return _sockets_nb; };
        inline int get_cores_nb() { return _cores_nb; };
        inline int get_levels() { return _levels; };
        inline int get_per_core_capacity() { return _per_core_capacity; };
        inline int get_llc_capacity() { return _llc_capacity; };
        inline int get_total_capacity() { return _total_capacity; };
        inline HashMap<CpuCacheId *, CpuCache *> &get_cache_hierarchy() { return _cache_hierarchy; };

        void add_cache(CpuCache *cache);
        void compute_total_capacity();
        void query();
        void print();
        Json to_json();

        static const int MAX_CPU_CACHE_LEVELS = 4;
        static const int BYTES_IN_KILO_BYTE = 1024;

    private:
        CpuVendor _vendor;
        int _sockets_nb;          // Number of CPU sockets
        int _cores_nb;            // Number of CPU cores
        int _levels;              // Number of cache levels
        long _per_core_capacity;  // CPU cache capacity per core (kB)
        long _llc_capacity;       // Shared CPU cache capacity per socket (kB)
        long _total_capacity;     // Total CPU cache capacity (kB)
        HashMap<CpuCacheId *, CpuCache *> _cache_hierarchy;
};

class MemoryStats {
    public:
        MemoryStats();
        MemoryStats &operator=(MemoryStats &m);
        ~MemoryStats();

        inline long get_memory_used() { return _used; };
        inline long get_memory_free() { return _free; };
        inline long get_memory_total() { return _total; };

        inline long get_memory_used_gb() { return _used / KILO_TO_GIGA_BYTES; };
        inline long get_memory_free_gb() { return _free / KILO_TO_GIGA_BYTES; };
        inline long get_memory_total_gb() { return _total / KILO_TO_GIGA_BYTES; };

        void capacity_query();
        void utilization_query();

        void print();
        Json to_json();

        static const long KILO_TO_GIGA_BYTES = 1000000;

    private:
        long _used;
        long _free;
        long _total;  // kBytes
};

class MemoryModule {
    public:
        MemoryModule();
        MemoryModule(int id, MemoryType type, String manufacturer, String serial_nb,
                     int data_width, int total_width, long capacity, long speed,
                     long configured_speed);
        ~MemoryModule();

        inline void set_id(const int &id) { _id = id; };
        inline void set_type(const MemoryType &type) { _type = type; };
        inline void set_manufacturer(const String &manufacturer) { _manufacturer = manufacturer; };
        inline void set_serial_nb(const String &serial_nb) { _serial_nb = serial_nb; };
        inline void set_data_width(const int &data_width) { _data_width = data_width; };
        inline void set_total_width(const int &total_width) { _total_width = total_width; };
        inline void set_capacity(const long &capacity) { _capacity = capacity; };
        inline void set_speed(const long &speed) { _speed = speed; };
        inline void set_configured_speed(const long &configured_speed) { _configured_speed = configured_speed; };

        inline int get_id() { return _id; };
        inline MemoryType get_type() { return _type; };
        inline String &get_manufacturer() { return _manufacturer; };
        inline String &get_serial_number() { return _serial_nb; };
        inline int get_data_width() { return _data_width; };
        inline int get_total_width() { return _total_width; };
        inline long get_capacity() { return _capacity; };
        inline long get_speed() { return _speed; };
        inline long get_configured_speed() { return _configured_speed; };

        void print();
        Json to_json();

    private:
        int _id;
        MemoryType _type;
        String _manufacturer;
        String _serial_nb;
        int _data_width;
        int _total_width;
        long _capacity;
        long _speed;
        long _configured_speed;

};

class MemoryHierarchy {
    public:
        MemoryHierarchy();
        ~MemoryHierarchy();

        inline int get_modules_number() { return _modules_nb; };
        inline long get_total_capacity() { return _total_capacity; };
        inline MemoryModule *get_module(const unsigned &i) { return _memory_modules[i]; };
        inline MemoryStats &get_memory_stats() { return _stats; };

        void capacity_query();
        void query();
        long get_module_capacity();
        long get_module_speed();
        void fill_missing();
        void print();
        Json to_json();

    private:
        int _modules_nb;
        long _total_capacity;
        Vector<MemoryModule *> _memory_modules;
        MemoryStats _stats;
};

class NIC {
    public:
        NIC *mirror;

        NIC(bool verbose = false);
        NIC(const NIC &n);
        ~NIC();

        inline Element *get_element() { return _element; };
        inline bool is_ghost() { return (get_element() == NULL); };
        inline int get_index() { return is_ghost() ? -1 : _index; };
        inline bool is_active() { return cast()->is_active(); };
        inline portid_t get_port_id() { return is_ghost() ? -1 : cast()->get_device()->get_port_id(); };
        inline String get_name() { return is_ghost() ? "" : _element->name(); };
        inline String get_device_address() { return String(get_port_id()); };

        void set_element(Element *el);
        void set_index(const int &index);
        void set_active(const bool &active);

        FromDPDKDevice *cast();

    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        FlowDispatcher *get_flow_dispatcher(int sriov = 0);
        FlowCache *get_flow_cache(int sriov = 0);
    #endif

        int queue_per_pool();
        int phys_cpu_to_queue(int phys_cpu_id);

        Json to_json(const RxFilterType &rx_mode, const bool &stats = false);

        String call_rx_read(String h);
        String call_tx_read(String h);
        int    call_rx_write(String h, const String input);

    private:
        Element *_element; // Click element associated with this NIC
        int _index;        // Click port index of this NIC
        bool _verbose;     // Verbosity flag
};

class CPULayout {
    public:
        CPULayout(int sockets_nb, int cores_nb, int active_cores_nb,
                  String vendor, long frequency, String numa_nodes);
        ~CPULayout();

        inline int get_sockets_nb() { return _sockets_nb; };
        inline int get_cores_nb() { return _cores_nb; };
        inline int get_active_cores_nb() { return _active_cores_nb; };
        inline CPU *get_cpu_core(const int& id) { return _cpus[id]; };

        inline int get_phy_core_by_lcore(int lcore) { return _lcore_to_phy_core[lcore]; };
        inline int get_socket_by_lcore(int lcore) { return _lcore_to_socket[lcore]; };

        void compose();
        void print();

    private:
        int _sockets_nb;
        int _cores_nb;
        int _cores_per_socket;
        int _active_cores_nb;
        CpuVendor _vendor;
        long _frequency;
        String _numa_nodes;

        Vector<CPU *> _cpus;
        HashMap<int, int> _lcore_to_phy_core;
        HashMap<int, int> _lcore_to_socket;
};

class SystemResources {
    public:
        SystemResources(
            int sockets_nb, int cores_nb, int active_cores_nb, String vendor, long frequency,
            String numa_nodes, String hw, String sw, String serial, String chassis);
        ~SystemResources();

        inline void set_platform_info(const PlatformInfo &plat_info) { _plat_info = plat_info; };
        inline void set_cpu_layout(const CPULayout &cpu_layout) { _cpu_layout = cpu_layout; };
        inline void set_cpu_cache_hierarchy(const CpuCacheHierarchy &cache) { _cpu_cache_hierarchy = cache; };
        inline void set_memory(MemoryHierarchy &MemoryHierarchy) { _memory_hierarchy = _memory_hierarchy; };

        inline PlatformInfo &get_platform_info() { return _plat_info; };
        inline CPULayout &get_cpu_layout() { return _cpu_layout; };
        inline CpuCacheHierarchy &get_cpu_cache_hiearchy() { return _cpu_cache_hierarchy; };
        inline MemoryHierarchy &get_memory() { return _memory_hierarchy; };

        // Relay methods
        inline int get_cpu_sockets() { return _cpu_layout.get_sockets_nb(); };
        String get_cpu_vendor();
        inline String get_hw_info() { return _plat_info.get_hw_info(); };
        inline String get_sw_info() { return _plat_info.get_sw_info(); };
        inline String get_serial_number() { return _plat_info.get_serial_number(); };
        inline String get_chassis_id() { return _plat_info.get_chassis_id(); };

        void print();

    private:
        PlatformInfo _plat_info;
        CPULayout _cpu_layout;
        CpuCacheHierarchy _cpu_cache_hierarchy;
        MemoryHierarchy _memory_hierarchy;
};

class NicStat {
    public:
        NicStat() : _useless(0), _useful(0), _count(0), _load(0) {};
        ~NicStat() {};

        inline void set_useless(const long long &useless) { _useless = useless; };
        inline void set_useful(const long long &useful) { _useful = useful; };
        inline void set_count(const long long &count) { _count = count; };
        inline void set_useless(const float &load) { _load = load; };

        inline long long get_useless() { return _useless ; };
        inline long long get_useful() { return _useful ; };
        inline long long get_count() { return _count ; };
        inline float get_load() { return _load; };

    private:
        long long _useless;
        long long _useful;
        long long _count;
        float _load;
};

class Metron;

class ServiceChain {
    public:
        class RxFilter {
            public:
                RxFilter(ServiceChain *sc);
                ~RxFilter();

                RxFilterType method;
                ServiceChain *sc;

                static RxFilter *from_json(const Json &j, ServiceChain *sc, ErrorHandler *errh);
                Json to_json();
                void print();

                inline int phys_cpu_to_queue(NIC *nic, const int &phys_cpu_id) {
                    return nic->phys_cpu_to_queue(phys_cpu_id);
                }
                inline void allocate_nic_space_for_tags(const int &size);
                inline void allocate_tag_space_for_nic(const int &nic_id, const int &size);
                inline void set_tag_value(
                        const int &nic_id, const int &cpu_id, const String &value);
                inline String get_tag_value(const int &nic_id, const int &cpu_id);
                inline bool has_tag_value(const int &nic_id, const int &cpu_id);
                virtual int apply(NIC *nic, ErrorHandler *errh);

                Vector<Vector<String>> values;
        };

        /**
         * Public service chain attributes.
         */
        String id;
        RxFilter *rx_filter;
        String config;

        /**
         * Service chain type.
         */
        ScType config_type;

        enum ScStatus {
            SC_FAILED,
            SC_OK = 1
        };
        enum ScStatus status;

        ServiceChain(Metron *m);
        ~ServiceChain();

        void initialize_cpus(int initial_cpu_nb, int max_cpu_nb);

        static ServiceChain *from_json(const Json &j, Metron *m, ErrorHandler *errh);
        int reconfigure_from_json(Json j, Metron *m, ErrorHandler *errh);

        Json get_cpu_stats(int j);
        Json to_json();
        Json stats_to_json(bool monitoring_mode = false);
        void print();

    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        Json rules_to_json();
        int32_t rules_from_json(Json j, Metron *m, ErrorHandler *errh);
        static int delete_rule(const uint32_t &rule_id, Metron *m, ErrorHandler *errh);
        static int32_t delete_rules(const Vector<String> &rules_vec, Metron *m, ErrorHandler *errh);
        static int32_t delete_rule_batch_from_json(String rule_ids, Metron *m, ErrorHandler *errh);
    #endif

        inline String get_id() {
            return id;
        }

        inline RxFilterType get_rx_mode() {
            return rx_filter->method;
        }

        inline int get_active_cpu_nb() {
            int nb = 0;
            for (int i = 0; i < get_max_cpu_nb(); i++) {
                if (get_cpu_info(i).is_active()) {
                    nb++;
                }
            }
            return nb;
        }

        inline int get_max_cpu_nb() {
            return _max_cpus_nb;
        }

        inline CPUStats &get_cpu_info(int cpu_id) {
            return _cpus[cpu_id];
        }

        inline int get_cpu_phys_id(int cpu_id) {
            return _cpus[cpu_id].get_physical_id();
        }

        inline int get_nics_nb() {
            return _nics.size();
        }

        inline NIC *get_nic_by_name(String name) {
            for (NIC *nic : _nics) {
                if (nic->get_name() == name)
                    return nic;
            }
            return NULL;
        }

        inline int get_nic_index(NIC *nic) {
            for (int i = 0; i < _nics.size(); i++) {
                if (_nics[i] == nic)
                    return i;
            }
            return ERROR;
        }

        inline NIC *get_nic_by_index(int i) {
            return _nics[i];
        }

        Bitvector assigned_phys_cpus();
        Bitvector active_cpus();

        String generate_configuration(bool add_extra);
        String generate_configuration_slave_fd_name(
            const int &nic_index, const int &cpu_index, const String &type = "FD"
        );

        struct timing_stats {
            Timestamp start, parse, launch;
            Json to_json();
        };
        void set_timing_stats(struct timing_stats ts) {
            _timing_stats = ts;
        }

        struct autoscale_timing_stats {
            Timestamp autoscale_start, autoscale_end;
            Json to_json();
        };
        void set_autoscale_timing_stats(struct autoscale_timing_stats ts) {
            _as_timing_stats = ts;
        }

        void do_autoscale(int n_cpu_change);

        const unsigned short AUTOSCALE_WINDOW = 5000;

    private:
        Metron *_metron;
        ServiceChainManager *_manager;
        Vector<NIC *> _nics;
        Vector<CPUStats> _cpus;
        Vector<NicStat> _nic_stats;
        int _initial_cpus_nb;
        int _max_cpus_nb;
        float _total_cpu_load;
        float _max_cpu_load;
        int _max_cpu_load_index;

        struct timing_stats _timing_stats;
        struct autoscale_timing_stats _as_timing_stats;
        bool _autoscale;
        Timestamp _last_autoscale;
        bool _verbose;

        friend class Metron;
        friend class ServiceChainManager;
        friend class ClickSCManager;
        friend class StandaloneSCManager;
};

class Metron : public Element {
    public:
        Metron() CLICK_COLD;
        ~Metron() CLICK_COLD;

        const char *class_name() const  { return "Metron"; }
        const char *port_count() const  { return PORTS_0_0; }

        int configure_phase() const { return CONFIGURE_PHASE_LAST; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *) override CLICK_COLD;
        bool discover();
        void cleanup(CleanupStage) override CLICK_COLD;
        static int static_cleanup();

        static void discover_timer(Timer *timer, void *user_data);
        void run_timer(Timer *t) override;

        void add_handlers() override CLICK_COLD;
        static int param_handler(
            int operation, String &param, Element *e,
            const Handler *, ErrorHandler *errh
        ) CLICK_COLD;
        static String read_handler(Element *e, void *user_data) CLICK_COLD;
        static int write_handler(
            const String &data, Element *e, void *user_data,
            ErrorHandler *errh
        ) CLICK_COLD;
    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        static int rule_stats_handler(int operation, String &param, Element *e, const Handler *h, ErrorHandler *errh);
    #endif

        void sys_info_to_json(Json &j);

        Json to_json();
        Json time_to_json();
        Json system_stats_to_json();
        Json setup_link_discovery();
    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        Json nics_table_stats_to_json();
    #endif
        Json controllers_to_json();
        int  controllers_from_json(const Json &j);
        int  controller_delete_from_json(const String &ip);

        /**
         * Read and write handlers.
         */
        enum {
            h_server_connect, h_server_disconnect,
            h_server_discovered, h_server_time,
            h_server_resources, h_server_stats,

            h_nic_ports, h_nic_queues, h_nic_link_discovery,

            h_controllers, h_controllers_set, h_controllers_delete,

            h_service_chains, h_service_chains_put,
            h_service_chains_stats, h_service_chains_proxy,
            h_service_chains_delete,
        #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
            h_rules,
            h_rules_add_from_file,
            h_rules_table_stats,
            h_rules_verify,
            h_rules_delete,
            h_rules_flush,
            h_rule_inst_lat_min,  h_rule_inst_lat_avg,  h_rule_inst_lat_max,
            h_rule_del_lat_min,   h_rule_del_lat_avg,   h_rule_del_lat_max,
            h_rule_inst_rate_min, h_rule_inst_rate_avg, h_rule_inst_rate_max,
            h_rule_del_rate_min,  h_rule_del_rate_avg,  h_rule_del_rate_max
        #endif
        };

        ServiceChain *find_service_chain_by_id(const String &id);
        int instantiate_service_chain(ServiceChain *sc, ErrorHandler *errh);

        void kill_service_chain(ServiceChain *sc);
        int delete_service_chain(ServiceChain *sc, ErrorHandler *errh);
        void call_scale(ServiceChain *sc, const String &event);

        SystemResources *get_system_resources() { return _sys_res; }
        bool get_monitoring_mode() { return _monitoring_mode; }
        int get_cpus_nb() { return click_max_cpu_ids(); }

        NIC *get_nic_by_index(int i) {
            auto it = _nics.begin();
            while (it != _nics.end()) {
                NIC *nic = &it.value();
                if (nic->get_port_id() == (portid_t) i) {
                    return nic;
                }
                it++;
            }
            return NULL;
        }

        NIC *get_nic_by_name(String name) {
            auto it = _nics.begin();
            while (it != _nics.end()) {
                NIC *nic = &it.value();
                if (nic->get_name() == name) {
                    return nic;
                }
                it++;
            }
            return NULL;
        }

        int get_nics_nb() { return _nics.size(); }
        int get_service_chains_nb() { return _scs.size(); }
        int get_assigned_cpus_nb();

        bool assign_cpus(ServiceChain *sc, Vector<int> &map);
        void unassign_cpus(ServiceChain *sc);

        const float CPU_OVERLOAD_LIMIT = (float) 0.7;
        const float CPU_UNDERLOAD_LIMIT = (float) 0.4;

        /* Agent's default REST configuration */
        const int    DEF_AGENT_PORT  = 80;
        const String DEF_AGENT_PROTO = "http";

        /* Controller's default REST configuration */
        const int    DEF_DISCOVER_PORT      = 80;
        const int    DEF_DISCOVER_REST_PORT = 8181;
        const String DEF_DISCOVER_DRIVER    = "rest-server";
        const String DEF_DISCOVER_USER      = "onos";
        const String DEF_DISCOVER_PATH      = "/onos/v1/network/configuration/";

        /* Bound the discovery process */
        const unsigned DISCOVERY_WAIT = 5;

    private:
        String _agent_id;
        SystemResources *_sys_res;
        int _core_id;

        HashMap<String, NIC> _nics;
        HashMap<String, ServiceChain *> _scs;

        Vector<ServiceChain *> _sc_to_core_map;

        /* Rx filter mode */
        RxFilterType _rx_mode;

        /* Agent's (local) information */
        String _agent_ip;
        int    _agent_port;

        /* Controller's (remote) information */
        String _discover_ip;
        int    _discover_port;      // Port that talks to agent (Metron protocol)
        int    _discover_rest_port; // REST port
        String _discover_path;
        String _discover_user;
        String _discover_password;

        /* Discovery status */
        bool _discovered;

        /* Monitoring mode */
        bool _monitoring_mode;

        /* Fail on service chain instanciation error */
        bool _fail;

        /* Timer for load computation (msec) */
        unsigned _load_timer;

        /* Handler to call on scaling of some service chains */
        HandlerCall _on_scale;

        /* DPDK arguments for slave processes */
        Vector<String> _args;
        Vector<String> _dpdk_args;
        String _slave_extra;
        String _slave_td_args;

        /* Verbose */
        bool _verbose;

        /* Mirror */
        bool _mirror;

        /**
         * Click IDs to Physical IDs.
         * Important when launching DPDK slaves as we must not use unallowed CPUs.
         */
        Vector<int> _cpu_click_to_phys;

        Timer _timer;
        Timer _discover_timer;

        Spinlock _command_lock;

        int connect(const Json &j);
        int disconnect(const Json &j);
        int nic_port_administrator(const Json &j);
        Json nic_queues_report();
        void collect_system_resources();

        int try_slaves(ErrorHandler *errh);

        int confirm_nic_mode(ErrorHandler *errh);
    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        int flush_nics();
    #endif

        static void add_per_core_monitoring_data(
            Json *jobj, LatencyStats &lat
        );

        friend class ServiceChain;
        friend class ClickSCManager;
};

CLICK_ENDDECLS

#endif
