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
#include "../json/json.hh"

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
 * |-> Flow-based using Flow Director
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
    ON_SCALE, VERBOSE
    [, SLAVE_DPDK_ARGS, SLAVE_ARGS]
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

=over 18

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
elements must be configured with MODE flow_dir.
The Metron controller sends the rules to be installed in the NIC.
There rules ressemble a typical match-action API, where one of the
actions is to dispatch the matched flow to a certain hardware queue,
where a CPU core is waiting for additional processing.
2) MAC: The NIC utilizes the destination MAC address of incoming
packets for dispatching to the correct CPU core, using Virtual
Machine Device queues (VMDq). FromDPDKDevice elements must be
configured with MODE vmdq.
This mode requires an additional network element (e.g., a programmable
switch), prior to the Metron server, to set the destination MAC address
of each packet according to the values advertize by the Metron agent.
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

=item SLAVE_ARGS

String. DPDK arguments to pass to the primary DPDK process, which is the
Metron data plane agent.

=item SLAVE_DPDK_ARGS

String. DPDK arguments to pass to the deployed service chain instances,
which typically are secondary DPDK processes. For example, the following
arguments could be passed: '-b 03:00.0' if you want a certain NIC to be
blacklisted by a service chain.

=item VERBOSE

Boolean. If true, more detailed messages about Metron are printed.
Defaults to false.

=back

=h discovered read-only

Returns whether the Metron agent is associated with a controller or not.

=h resources read-only

Returns a JSON object with information about the Metron agent.

=h stats read-only

Returns a JSON object with global statistics about the Metron agent.

=h controllers read/write

Returns or sets the conteoller instance associated with this Metron agent.

=h chains read/write

Returns the currently deployed service chains or instantiates a set of new
service chains encoded as a JSON object.

=h chains_stats read

Returns a JSON object with either all service chain-level statistics of the
deployed service chains or statistics only for a desired service chain.

=h put_chains write

Reconfigures a set of already deployed service chains encoded as a JSON object.

=h rules read/write

Returns or sets the rules associated with either all deployed service chains or a specific
service chain.

=h delete_chains write

Tears down a deployed service chain.

=h delete_rules write

Removes the rules associated with a service chain or all service chains.

=h delete_controllers write

Disassociates this Metron agent from a Metron controller instance.

*/

// Return status
const int ERROR = -1;
const int SUCCESS = 0;

class Metron;

class CPU {
    public:
        CPU(int id, String vendor, long _frequency)
            : _id(id), _vendor(vendor), _frequency(_frequency) {
        }

        int get_id();
        String get_vendor();
        long get_frequency();

        Json to_json();

        static const int MEGA_HZ = 1000000;

    private:
        int _id;
        String _vendor;
        long _frequency;
};

class NIC {
    public:
        NIC(bool verbose = false)
            : _index(-1),
        #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
            _rules(), _internal_rule_map(),
        #endif
            _verbose(verbose) {

        }

        ~NIC() {
        #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
            remove_rules();
        #endif
        }

        Element *element;

        inline bool is_ghost() {
            return element == NULL;
        }

        portid_t get_port_id();
        String get_device_address();
        String get_name();
        int get_index();
        void set_index(const int &index);

    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        HashMap<long, String> *find_rules_by_core_id(const int &core_id);
        Vector<String> rules_list_by_core_id(const int &core_id);
        Vector<int> cores_with_rules();
        bool has_rules() { return !_rules.empty(); }
        bool insert_rule(const int &core_id, const long &rule_id, String &rule);
        int  install_rule(const long &rule_id, String &rule);
        bool remove_rule(const long &rule_id);
        bool remove_rule(const int &core_id, const long &rule_id);
        bool update_rule(const int &core_id, const long &rule_id, String &rule);
        bool remove_rules();
    #endif

        Json to_json(RxFilterType rx_mode, bool stats = false);

        int queue_per_pool();

        int phys_cpu_to_queue(int phys_cpu_id) {
		assert(phys_cpu_id >= 0);
            return phys_cpu_id * (queue_per_pool());
        }

        String call_rx_read(String h);
        String call_tx_read(String h);
        int    call_rx_write(String h, const String input);

    private:
    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        // Maps CPU cores to a map of rule ID -> rule
        HashMap<int, HashMap<long, String> *> _rules;

        // Maps ONOS rule IDs (long) to NIC rule IDs (uint32_t)
        HashMap<long, uint32_t> _internal_rule_map;
    #endif

        // Click port index of this NIC
        int _index;

        bool _verbose;

    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        // Methods that facilitate the mapping between ONOS and NIC rule IDs
        uint32_t get_internal_rule_id(const long &rule_id);
        bool verify_unique_rule_id_mapping(const uint32_t &int_rule_id);
        bool store_rule_id_mapping(const long &rule_id, const uint32_t &int_rule_id);
        bool delete_rule_id_mapping(const long &rule_id);
    #endif

};

struct LatencyInfo {
    uint64_t avg_throughput;
    uint64_t min_latency;
    uint64_t average_latency;
    uint64_t max_latency;
};

class CpuInfo {
public:
	CpuInfo() : cpuPhysId(-1),load(0),maxNicQueue(0),latency(),_active(false),_active_time() {

	}

	int cpuPhysId;
    float load;
    int maxNicQueue;

    LatencyInfo latency;

    bool assigned() {
	return cpuPhysId >= 0;
    }

    void set_active(bool active) {
	_active = active;
	_active_time = Timestamp::now();
    }

    bool active() {
	return _active;
    }

    int activeSince() {
	int cpu_time;
        if (_active)
            cpu_time = (Timestamp::now() - _active_time).msecval();
        else
            cpu_time = -(Timestamp::now() - _active_time).msecval();
        return cpu_time;
    }

private:
    bool _active;
    Timestamp _active_time;

};

class NicStat {
  public:
	long long useless;
	long long useful;
	long long count;
	float load;

	NicStat() : useless(0), useful(0), count(0), load(0) {
	}
};



class ServiceChain {
    public:
        class RxFilter {
          public:

			RxFilter(ServiceChain *sc);
			~RxFilter();

			RxFilterType method;
			ServiceChain *_sc;

			static RxFilter *from_json(
				Json j, ServiceChain *sc, ErrorHandler *errh
			);
			Json to_json();

			inline int phys_cpu_to_queue(NIC *nic, int phys_cpu_id) {
				return nic->phys_cpu_to_queue(phys_cpu_id);
			}

			inline void allocate_nic_space_for_tags(const int size);

			inline void allocate_tag_space_for_nic(const int nic_id, const int size);

			inline void set_tag_value(
					const int nic_id, const int cpu_id, const String value);

			inline String get_tag_value(const int nic_id, const int cpu_id);

			inline bool has_tag_value(const int nic_id, const int cpu_id);

			virtual int apply(NIC *nic, ErrorHandler *errh);

			Vector<Vector<String>> values;
        };

        /**
         * Service chain public attributes.
         */
        String id;
        RxFilter *rx_filter;
        String config;

        // Service chain type
        ScType config_type;

        enum ScStatus {
            SC_FAILED,
            SC_OK = 1
        };
        enum ScStatus status;

        /**
         * Service chain methods.
         */
        ServiceChain(Metron *m);
        ~ServiceChain();

        void initializeCpus(int initialCpuNb, int maxCpuNb);

        static ServiceChain *from_json(Json j, Metron *m, ErrorHandler *errh);
        int reconfigure_from_json(Json j, Metron *m, ErrorHandler *errh);

        Json get_cpu_stats(int j);
        Json to_json();
        Json stats_to_json(bool monitoring_mode = false);

    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        Json rules_to_json();
        int rules_from_json(Json j, Metron *m, ErrorHandler *errh);
        static int delete_rule_from_json(
            const long rule_id, Metron *m, ErrorHandler *errh
        );
        static int delete_rule_batch_from_json(
            String rule_ids, Metron *m, ErrorHandler *errh
        );
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
                if (get_cpu_info(i).active()) {
                    nb++;
                }
            }
            return nb;
        }

        inline int get_max_cpu_nb() {
            return _max_cpus_nb;
        }

        inline CpuInfo& get_cpu_info(int cpu_id) {
            return _cpus[cpu_id];
        }

        inline int get_cpu_phys_id(int cpu_id) {
            return _cpus[cpu_id].cpuPhysId;
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

       // Bitvector assigned_cpus();
        Bitvector active_cpus();

        String generate_configuration();
        String generate_configuration_slave_fd_name(int nic_index, int cpu_index, String type = "FD" );

        Vector<String> build_cmd_line(int socketfd);

        void control_init(int fd, int pid);

        int control_read_line(String &line);

        void control_write_line(String cmd);

        String control_send_command(String cmd);

        void check_alive();

        int call(
            String fnt, bool has_response, String handler,
            String &response, String params
        );
        String simple_call_read(String handler);
        String simple_call_write(String handler);
        int call_read(String handler, String &response, String params = "");
        int call_write(String handler, String &response, String params = "");

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

        void do_autoscale(int nCpuChange);

        const unsigned short AUTOSCALE_WINDOW = 5000;

    private:
        Metron *_metron;
        Vector<NIC *> _nics;
        Vector<CpuInfo> _cpus;
        Vector<NicStat> _nic_stats;
        float _total_cpu_load;
        float _max_cpu_load;
        int _max_cpu_load_index;
        int _socket;
        int _pid;
        struct timing_stats _timing_stats;
        struct autoscale_timing_stats _as_timing_stats;
        int _initial_cpus_nb;
        int _max_cpus_nb;
        bool _autoscale;
        Timestamp _last_autoscale;
        bool _verbose;

        friend class Metron;
};

/*
=c

Metron */

class Metron : public Element {
    public:
        Metron() CLICK_COLD;
        ~Metron() CLICK_COLD;

        const char *class_name() const  { return "Metron"; }
        const char *port_count() const  { return PORTS_0_0; }

        int configure_phase() const { return CONFIGURE_PHASE_LAST; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        bool discover();
        void cleanup(CleanupStage) CLICK_COLD;

        static void discover_timer(Timer *timer, void *user_data);
        void run_timer(Timer *t) override;

        void add_handlers() CLICK_COLD;
        static int param_handler(
            int operation, String &param, Element *e,
            const Handler *, ErrorHandler *errh
        ) CLICK_COLD;
        static String read_handler(Element *e, void *user_data) CLICK_COLD;
        static int write_handler(
            const String &data, Element *e, void *user_data,
            ErrorHandler *errh
        ) CLICK_COLD;

        void hw_info_to_json(Json &j);

        Json to_json();
        Json stats_to_json();
        Json controllers_to_json();
        int  controllers_from_json(Json j);
        int  delete_controller_from_json(const String &ip);

        // Read and write handlers
        enum {
            h_discovered, h_resources, h_controllers, h_stats,
            h_put_chains, h_chains, h_chains_stats, h_chains_proxy,
        #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
            h_chains_rules,
        #endif
            h_delete_chains, h_delete_controllers,
        #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
            h_delete_rules
        #endif
        };

        ServiceChain *find_service_chain_by_id(String id);
        int instantiate_service_chain(ServiceChain *sc, ErrorHandler *errh);

        void kill_service_chain(ServiceChain *sc);
        int remove_service_chain(ServiceChain *sc, ErrorHandler *errh);
        void call_scale(ServiceChain*, String);

        bool get_monitoring_mode() {
            return _monitoring_mode;
        }

        int get_cpus_nb() {
            return click_max_cpu_ids();
        }

        int get_nics_nb() {
            return _nics.size();
        }

        int get_service_chains_nb() {
            return _scs.size();
        }

        int get_assigned_cpus_nb();

        bool assign_cpus(ServiceChain *sc, Vector<int> &map);
        void unassign_cpus(ServiceChain *sc);

        const float CPU_OVERLOAD_LIMIT = (float) 0.7;
        const float CPU_UNERLOAD_LIMIT = (float) 0.4;

        /* Agent's default REST configuration */
        const int    DEF_AGENT_PORT  = 80;
        const String DEF_AGENT_PROTO = "http";

        /* Controller's default REST configuration */
        const int    DEF_DISCOVER_PORT      = 80;
        const int    DEF_DISCOVER_REST_PORT = 8181;
        const String DEF_DISCOVER_DRIVER    = "restServer";
        const String DEF_DISCOVER_USER      = "onos";
        const String DEF_DISCOVER_PATH      = "/onos/v1/network/configuration/";

        /* Bound the discovery process */
        const unsigned DISCOVERY_WAIT = 5;

    private:
        String _id;
        int _core_id;

        HashMap<String, NIC> _nics;
        HashMap<String, ServiceChain *> _scs;

        Vector<ServiceChain *> _cpu_map;

        String _cpu_vendor;
        String _hw;
        String _sw;
        String _serial;

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

        /* Verbose */
        bool _verbose;

        /* Private methods */
        int try_slaves(ErrorHandler *errh);
        int run_service_chain(ServiceChain *sc, ErrorHandler *errh);
        int confirm_nic_mode(ErrorHandler *errh);

        static void add_per_core_monitoring_data(
            Json  *jobj,
            const LatencyInfo lat
        );

        Timer _timer;
        Timer _discover_timer;

        Spinlock _command_lock;
        friend class ServiceChain;
};

CLICK_ENDDECLS

#endif
