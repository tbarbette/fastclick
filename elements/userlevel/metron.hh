// -*- mode: c++; c-basic-offset: 4 -*-

#ifndef CLICK_METRON_HH
#define CLICK_METRON_HH

#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/task.hh>
#include <click/notifier.hh>
#include <click/hashmap.hh>

#include "../json/json.hh"

#define SC_CONF_TYPES \
    sctype(UNKNOWN), \
    sctype(CLICK),   \
    sctype(STANDALONE)

#define sctype(x) x

/**
 * The service chain types supported by Metron:
 * |-> Click-based
 * |-> Standalone (for integration with blackbox NFs)
 */
typedef enum { SC_CONF_TYPES } ScType;

#undef sctype
#define sctype(x) #x

CLICK_DECLS

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
        Element *element;

        String get_id() {
            return element->name();
        }

        String get_device_id();

        Json to_json(bool stats = false);

        int queue_per_pool() {
            return atoi(
                call_read("nb_rx_queues").c_str()) /
                atoi(call_read("nb_vf_pools").c_str()
            );
        }

        int cpu_to_queue(int id) {
            return id * (queue_per_pool());
        }

        String call_read(String h);
        String call_tx_read(String h);
};

class ServiceChain {
    public:
        class RxFilter {
            public:

                RxFilter(ServiceChain *sc) : _sc(sc) {

                }

                ~RxFilter() {

                }

                String method;
                ServiceChain *_sc;

                static RxFilter *from_json(
                    Json j, ServiceChain *sc, ErrorHandler *errh
                );
                Json to_json();

                int cpu_to_queue(NIC *nic, int cpuid) {
                    return nic->cpu_to_queue(cpuid);
                }

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

        class Stat {
            public:
                long long useless;
                long long useful;
                long long count;
                float load;

                Stat() : useless(0), useful(0), count(0), load(0) {

                }
        };
        Vector<Stat> nic_stats;

        /**
         * Service chain methods.
         */
        ServiceChain(Metron *m);
        ~ServiceChain();

        static ServiceChain *from_json(Json j, Metron *m, ErrorHandler *errh);
        int reconfigureFromJSON(Json j, Metron *m, ErrorHandler *errh);

        Json to_json();
        Json stats_to_json();

        inline String get_id() {
            return id;
        }

        inline int get_used_cpu_nb() {
            return _used_cpus_nb;
        }

        inline int get_max_cpu_nb() {
            return _max_cpus_nb;
        }


        inline int get_cpu_map(int i) {
            return _cpus[i];
        }

        inline int get_nics_nb() {
            return _nics.size();
        }

        inline NIC *get_nic_by_id(String id) {
            for (NIC *nic : _nics) {
                if (nic->get_id() == id)
                    return nic;
            }

            return 0;
        }

        inline int get_nic_index(NIC *nic) {
            for (int i = 0; i < _nics.size(); i++) {
                if (_nics[i] == nic)
                    return i;
            }

            return -1;
        }

        inline NIC *get_nic_by_index(int i) {
            return _nics[i];
        }

        Bitvector assigned_cpus();

        String generate_config();
        String generate_config_slave_fd_name(int nic_index, int cpu_index) {
            return "slaveFD" + String(nic_index) + "C" + String(cpu_index);
        }

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
        int call_read(String handler, String &response, String params = "");
        int call_write(String handler, String &response, String params = "");

        Vector<int> &get_cpu_map_ref() {
            return _cpus;
        }

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

    private:
        Metron *_metron;
        Vector<int> _cpus;
        Vector<NIC *> _nics;
        Vector<float> _cpuload;
        float _total_cpuload;
        int _socket;
        int _pid;
        struct timing_stats _timing_stats;
        struct autoscale_timing_stats _as_timing_stats;
        int _used_cpus_nb;
        int _max_cpus_nb;
        bool _autoscale;
        Timestamp _last_autoscale;

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

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        bool discover();
        void cleanup(CleanupStage) CLICK_COLD;

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

        void set_hw_info(Json &j);

        Json to_json();
        Json stats_to_json();
        Json controllers_to_json();
        int  controllers_from_json(Json j);
        int  delete_controllers_from_json(void);

        // Read and write handlers
        enum {
            h_discovered, h_controllers,
            h_resources,  h_stats,
            h_delete_controllers, h_delete_chains, h_put_chains,
            h_chains, h_chains_stats, h_chains_proxy
        };

        ServiceChain *find_chain_by_id(String id);

        int instantiate_chain(ServiceChain *sc, ErrorHandler *errh);
        int remove_chain(ServiceChain *sc, ErrorHandler *errh);

        int get_cpus_nb() {
            return click_max_cpu_ids();
        }

        int get_chains_nb() {
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
        const unsigned short DISCOVERY_ATTEMPTS = 3;

    private:
        String _id;
        Vector<String> _args;
        Vector<String> _dpdk_args;

        HashMap<String, NIC> _nics;
        HashMap<String, ServiceChain *> _scs;

        Vector<ServiceChain *> _cpu_map;

        String _cpu_vendor;
        String _hw;
        String _sw;
        String _serial;

        bool _timing_stats;

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

        int runChain(ServiceChain *sc, ErrorHandler *errh);

        Timer _timer;

        friend class ServiceChain;
};

CLICK_ENDDECLS

#endif
