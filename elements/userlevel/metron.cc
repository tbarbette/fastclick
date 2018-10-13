// -*- c-basic-offset: 4; related-file-name: "metron.hh" -*-
/*
 * metron.{cc,hh} -- element that deploys, monitors, and
 * (re)configures NFV service chains driven by a remote
 * controller
 *
 * Copyright (c) 2017 Tom Barbette, University of Liège
 * Copyright (c) 2017 Georgios Katsikas, RISE SICS and
 *                    KTH Royal Institute of Technology
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

#include "fromdpdkdevice.hh"
#include "todpdkdevice.hh"
#include "metron.hh"

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    #include <click/flowdirector.hh>
#endif

#if HAVE_CURL
    #include <curl/curl.h>
#endif

CLICK_DECLS

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
HashMap<uint32_t, struct Metron::rule_timing_stats> Metron::_rule_inst_stats_map;
HashMap<uint32_t, struct Metron::rule_timing_stats> Metron::_rule_del_stats_map;
#endif

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
    return CLICK;
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

/**
 * Parses input string and returns information after key.
 */
static String
parse_vendor_info(const String &hw_info, const String &key)
{
    String s;

    s = hw_info.substring(hw_info.find_left(key) + key.length());
    int pos = s.find_left(':') + 2;
    s = s.substring(pos, s.find_left("\n") - pos);

    return s;
}

/***************************************
 * Metron
 **************************************/
Metron::Metron() :
    _timer(this), _rx_mode(FLOW), _discover_timer(&discover_timer, this),
    _discover_ip(), _discovered(false), _monitoring_mode(false),
    _fail(false), _load_timer(1000), _verbose(false)
{
    _core_id = click_max_cpu_ids() - 1;
}

Metron::~Metron()
{
    _nics.clear();
    _scs.clear();
    _cpu_map.clear();
    _args.clear();
    _dpdk_args.clear();
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

    if (Args(conf, this, errh)
        .read    ("ID",                _id)
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
        .read_all("SLAVE_DPDK_ARGS",   _dpdk_args)
        .read_all("SLAVE_ARGS",        _args)
        .read    ("SLAVE_EXTRA",       _slave_extra)
        .read    ("VERBOSE",           _verbose)
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
    errh->message(
        "Rx filter mode: %s",
        rx_filter_type_enum_to_str(_rx_mode).c_str()
    );

    if (_load_timer <= 0) {
        return errh->error("Set a positive scheduling frequency using LOAD_TIMER");
    }

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
            "Provide your local IP and a username & password to "
            "access Metron controller's REST API"
        );
    }

    // Ports must strictly become positive uint16_t
    if (        (_agent_port <= 0) ||         (_agent_port > UINT16_MAX) ||
        (_discover_rest_port <= 0) || (_discover_rest_port > UINT16_MAX)) {
        return errh->error("Invalid port number");
    }
#endif

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
        nic.element = e;
        _nics.insert(nic.get_name(), nic);
    }

    // Confirm the mode of the underlying NICs
    return confirm_nic_mode(errh);
}

/**
 * Verifies that Metron configuration complies with the
 * underlying FromDPDKDevice elements' configuration.
 */
int
Metron::confirm_nic_mode(ErrorHandler *errh)
{
    auto nic = _nics.begin();
    while (nic != _nics.end()) {
        // Cast input element
        FromDPDKDevice *fd = dynamic_cast<FromDPDKDevice *>(nic.value().element);

        if (!fd->get_device())
            continue;

        // Get its Rx mode
        String fd_mode = fd->get_device()->get_mode_str();

    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        // TODO: What if none of the NICs is in Metron mode?
        if ((_rx_mode == FLOW) && (fd_mode != FlowDirector::FLOW_DIR_MODE)) {
            errh->warning(
                "[NIC %s] Configured in MODE %s, which is incompatible with Metron's accurate dispatching",
                nic.value().get_name().c_str(), fd_mode.c_str()
            );
        }
    #endif

        // TODO: What if _rx_mode = VLAN and fd_mode = vmdq?
        //       The agent should be able to provide MAC or VLAN tags.
        if ((_rx_mode == MAC) && (fd_mode != "vmdq")) {
            return errh->error(
                "Metron RX_MODE %s requires FromDPDKDevice(%s) MODE vmdq",
                rx_filter_type_enum_to_str(_rx_mode).c_str(),
                nic.value().get_name().c_str()
            );
        }

        if ((_rx_mode == RSS) && (fd_mode != "rss")) {
            return errh->error(
                "RX_MODE %s is the default FastClick mode and requires FromDPDKDevice(%s) MODE rss. Current mode is %s.",
                rx_filter_type_enum_to_str(_rx_mode).c_str(),
                nic.value().get_name().c_str(), fd_mode.c_str()
            );
        }

        nic++;
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
    if (_id.empty()) {
        _id = "metron:nfv:dataplane:";
        String uuid = shell_command_output_string("cat /proc/sys/kernel/random/uuid", "", errh);
        uuid = uuid.substring(0, uuid.find_left("\n"));
        _id = (!uuid || uuid.empty())? _id + "00000000-0000-0000-0000-000000000001" : _id + uuid;
    }


    if (_on_scale)
        if (_on_scale.initialize_write(this, errh) < 0)
            return -1;

    _cpu_map.resize(get_cpus_nb(), 0);

    String hw_info = file_string("/proc/cpuinfo");
    _cpu_vendor = parse_vendor_info(hw_info, "vendor_id");
    _hw = parse_vendor_info(hw_info, "model name");
    _sw = CLICK_VERSION;

    String sw_info = shell_command_output_string("dmidecode -t 1", "", errh);
    _serial = parse_vendor_info(sw_info, "Serial Number");

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

    assert(DPDKDevice::initialized());
    if (try_slaves(errh) != SUCCESS) {
        return ERROR;
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
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    _rule_inst_stats_map.clear();
    _rule_del_stats_map.clear();
#endif
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
    sc.initialize_cpus(1,1);
    for (int i = 0; i < sc.get_nics_nb(); i++) {
        NIC *nic = sc.get_nic_by_index(i);
        sc._nics.push_back(nic);
    }
    sc._nic_stats.resize(sc._nics.size() * 1, NicStat());
    sc.rx_filter = new ServiceChain::RxFilter(&sc);
    Vector<int> cpu_phys_map;
    cpu_phys_map.resize(1);
    assign_cpus(&sc, cpu_phys_map);
    assert(cpu_phys_map[0] >= 0);
    sc.get_cpu_info(0).cpu_phys_id = cpu_phys_map[0];
    sc.get_cpu_info(0).set_active(true);
    if (run_service_chain(&sc, errh) != 0) {
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
        struct curl_slist *headers=NULL;
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
            rest.set("username", _id);
            rest.set("password", "");
            rest.set("ip", _agent_ip);
            rest.set("port", _agent_port);
            rest.set("protocol", DEF_AGENT_PROTO);
            rest.set("url", "");
            rest.set("testUrl", "");
            rest.set("isProxy", false);
            hw_info_to_json(rest);
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
    double alpha_up = 0.5;
    double alpha_down = 0.3;
    double total_alpha = 0.5;

    int sn = 0;
    while (sci != _scs.end()) {
        ServiceChain *sc = sci.value();
        float max_cpu_load = 0;
        int max_cpu_load_index = 0;
        float total_cpu_load = 0;

        Vector<String> min = sc->simple_call_read("monitoring_lat.mp_min").split(' ');
        Vector<String> max = sc->simple_call_read("monitoring_lat.mp_max").split(' ');
        Vector<String> avg = sc->simple_call_read("monitoring_lat.mp_average_time").split(' ');
        sc->simple_call_write("monitoring_lat.reset");
        Vector<String> load = sc->simple_call_read("load").split(' ');


        for (int j = 0; j < sc->get_max_cpu_nb(); j++) {

            const int cpu_id = sc->get_cpu_phys_id(j);
            String js = String(j);
            float cpu_load = 0;
            float cpu_queue = 0;
            uint64_t throughput = 0;
            for (int i = 0; i < sc->get_nics_nb(); i++) {
                String is = String(i);
                NIC *nic = sc->get_nic_by_index(i);
                assert(nic);
                int stat_idx = (j * sc->get_nics_nb()) + i;

                String name = sc->generate_configuration_slave_fd_name(i, cpu_id);
//                long long useless = atoll(sc->simple_call_read(name + ".useless").c_str());
//                long long useful = atoll(sc->simple_call_read(name + ".useful").c_str());
                long long count = atoll(sc->simple_call_read(name + ".count").c_str());
//                long long useless_diff = useless - sc->nic_stats[stat_idx].useless;
//                long long useful_diff = useful - sc->nic_stats[stat_idx].useful;
                long long count_diff = count - sc->_nic_stats[stat_idx].count;
//                sc->nic_stats[stat_idx].useless = useless;
//                sc->nic_stats[stat_idx].useful = useful;
                sc->_nic_stats[stat_idx].count = count;
//                long long count = atoll(sc->simple_call_write(name + ".reset_load").c_str());
//                if (useful_diff + useless_diff == 0) {
//                    sc->nic_stats[stat_idx].load = 0;
                    // click_chatter(
                    //      "[SC %d] Load NIC %d CPU %d - %f: No data yet",
                    //      sn, i, j, sc->nic_stats[stat_idx].load
                    //  );
//                    continue;
//                }
/*                double load = (double)useful_diff / (double)(useful_diff + useless_diff);
                double alpha;
                if (load > sc->nic_stats[stat_idx].load) {
                    alpha = alpha_up;
                } else {
                    alpha = alpha_down;
                }
                sc->nic_stats[stat_idx].load = (sc->nic_stats[stat_idx].load * (1-alpha)) + ((alpha) * load);
*/
                //sc->nic_stats[stat_idx].load = sc->simple_call_read(
                // click_chatter(
                //      "[SC %d] Load NIC %d CPU %d - %f %f - diff usefull %lld useless %lld",
                //      sn, i, j, load, sc->nic_stats[stat_idx].load, useful_diff, useless_diff
                //  );

               /* if (sc->nic_stats[stat_idx].load > cpu_load) {
                    cpu_load = sc->nic_stats[stat_idx].load;
                }*/
                throughput += atoll(sc->simple_call_read("monitoring_th_" + is + "_" + js + ".link_rate").c_str());
                assert(sc->get_cpu_info(j).assigned());
                float ncpuqueue = (float)atoi(sc->simple_call_read(name + ".queue_count "+String(nic->phys_cpu_to_queue(sc->get_cpu_phys_id(j)))).c_str()) / (float)(atoi(nic->call_tx_read("nb_rx_desc").c_str()));
                if (ncpuqueue > cpu_queue) {
                    cpu_queue = ncpuqueue;
                }
            }
            cpu_load = atof(load[j].c_str());
            sc->_cpus[j].load = cpu_load;
            sc->_cpus[j].max_nic_queue = cpu_queue;
            if (_monitoring_mode) {
                sc->_cpus[j].latency.avg_throughput = throughput;
                sc->_cpus[j].latency.min_latency = atoll(min[j].c_str());
                sc->_cpus[j].latency.max_latency = atoll(max[j].c_str());
                sc->_cpus[j].latency.average_latency = atoll(avg[j].c_str());
            }
            total_cpu_load += cpu_load;
            if (cpu_load > max_cpu_load) {
               max_cpu_load = cpu_load;
               max_cpu_load_index = j;
            }
        }

        if (sc->_autoscale) {
            sc->_total_cpu_load = sc->_total_cpu_load *
                                  (1 - total_alpha) + max_cpu_load * (total_alpha);
            if (sc->_total_cpu_load > CPU_OVERLOAD_LIMIT) {
                sc->do_autoscale(1);
            } else if (sc->_total_cpu_load < CPU_UNERLOAD_LIMIT) {
                sc->do_autoscale(-1);
            }
        } else {
            sc->_total_cpu_load = sc->_total_cpu_load * (1 - total_alpha) +
                                  (total_cpu_load / sc->get_active_cpu_nb()) * (total_alpha);
        }
        sc->_max_cpu_load = max_cpu_load;
        sc->_max_cpu_load_index = max_cpu_load_index;

        sn++;
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
    for (int i = 0; i < get_cpus_nb(); i++) {
        if (_cpu_map[i] != 0) {
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

    for (int i = 0; i < _cpu_map.size(); i++) {
        if (_cpu_map[i] == 0) {
            _cpu_map[i] = sc;
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
    for (int i = 0; i < get_cpus_nb(); i++) {
        if (_cpu_map[i] == sc) {
            _cpu_map[i] = 0;
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
    Vector<int> cpu_phys_map;
    cpu_phys_map.resize(sc->get_max_cpu_nb());

    if (!assign_cpus(sc, cpu_phys_map)) {
        errh->error(
            "Cannot instantiate service chain %s: Not enough CPUs",
            sc->get_id().c_str()
        );
        return ERROR;
    }

    for (int i = 0; i < cpu_phys_map.size(); i++) {
        assert(cpu_phys_map[i] >= 0);
        sc->get_cpu_info(i).cpu_phys_id = cpu_phys_map[i];
    }
    for (int i = 0; i < sc->_initial_cpus_nb; i++) {
        sc->get_cpu_info(i).set_active(true);
    }

    int ret = run_service_chain(sc, errh);
    if (ret != SUCCESS) {
        unassign_cpus(sc);
        if (_fail) {
            abort();
        }
        return ERROR;
    }

    call_scale(sc, "start");

    sc->status = ServiceChain::SC_OK;
    _scs.insert(sc->get_id(), sc);

    return SUCCESS;
}

/**
 * Run a service chain and keep a control socket to it.
 * CPU cores must already be assigned by assign_cpus().
 */
int
Metron::run_service_chain(ServiceChain *sc, ErrorHandler *errh)
{
    for (int i = 0; i < sc->get_nics_nb(); i++) {
        if (sc->rx_filter->apply(sc->get_nic_by_index(i), errh) != 0) {
            return errh->error("Could not apply Rx filter");
        }
    }

    int config_pipe[2], ctl_socket[2];
    if (pipe(config_pipe) == -1)
        return errh->error("Could not create pipe");
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, ctl_socket) == -1)
        return errh->error("Could not create socket");

    // Launch slave
    int pid = fork();
    if (pid == -1) {
        return errh->error("Fork error. Too many processes?");
    }
    if (pid == 0) {
        int i, ret;

        close(0);
        dup2(config_pipe[0], 0);
        close(config_pipe[0]);
        close(config_pipe[1]);
        close(ctl_socket[0]);

        Vector<String> argv = sc->build_cmd_line(ctl_socket[1]);

        char *argv_char[argv.size() + 1];
        for (int i = 0; i < argv.size(); i++) {
            // click_chatter("Cmd line arg: %s", argv[i].c_str());
            argv_char[i] = strdup(argv[i].c_str());
        }
        argv_char[argv.size()] = 0;
        if ((ret = execv(argv_char[0], argv_char))) {
            errh->message("Could not launch slave process: %d %d", ret, errno);
        }

        exit(1);
    } else {
        click_chatter("Child %d launched successfully", pid);
        close(config_pipe[0]);
        close(ctl_socket[1]);
        int flags = 1;
        /*int fd = ctl_socket[0];
        if (ioctl(fd, FIONBIO, &flags) != 0) {
            flags = fcntl(fd, F_GETFL);
            if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
                return errh->error("%s", strerror(errno));
        }
        */
        String conf = sc->generate_configuration();

        click_chatter("Writing configuration %s", conf.c_str());

        int pos = 0;
        while (pos != conf.length()) {
            ssize_t r = write(
                config_pipe[1], conf.begin() + pos, conf.length() - pos
            );
            if (r == 0 || (r == -1 && errno != EAGAIN && errno != EINTR)) {
                if (r == -1) {
                    errh->message("%s while writing configuration", strerror(errno));
                }
                break;
            } else if (r != -1) {
                pos += r;
            }
        }

        if (pos != conf.length()) {
            close(config_pipe[1]);
            close(ctl_socket[0]);
            return ERROR;
        } else {
            close(config_pipe[1]);
            sc->control_init(ctl_socket[0], pid);
        }

        String s;
        int v = sc->control_read_line(s);
        if (v <= 0) {
            return errh->error("_assignedCould not read from control socket: Error %d", v);
        }
        if (!s.starts_with("Click::ControlSocket/1.")) {
            kill(pid, SIGKILL);
            return errh->error("Unexpected ControlSocket command");
        }

        return SUCCESS;
    }

    assert(0);
    return ERROR;
}

/**
 * Kill the service chain without cleaning any ressource
 */
void
Metron::kill_service_chain(ServiceChain *sc)
{
    _command_lock.acquire();
    sc->control_send_command("WRITE stop");
    _command_lock.release();
}

/**
 * Stop and remove a chain from the internal list, then unassign CPUs.
 * It is the responsibility of the caller to delete the chain.
 */
int
Metron::remove_service_chain(ServiceChain *sc, ErrorHandler *errh)
{
    // No controller
    if (!_discovered) {
        return errh->error(
            "Cannot remove service chain %s: Metron agent is not associated with a controller",
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
 * Metron agent's read handlers.
 */
String
Metron::read_handler(Element *e, void *user_data)
{
    Metron *m = static_cast<Metron *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    Json jroot = Json::make_object();

    switch (what) {
        case h_discovered: {
            return m->_discovered? "true" : "false";
        }
        case h_resources: {
            jroot = m->to_json();
            break;
        }
        case h_controllers: {
            jroot = m->controllers_to_json();
            break;
        }
        case h_stats: {
            jroot = m->stats_to_json();
            break;
        }
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
        case h_controllers: {
            return m->controllers_from_json(Json::parse(data));
        }
        case h_delete_chains: {
            ServiceChain *sc = m->find_service_chain_by_id(data);
            if (!sc) {
                return errh->error(
                    "Cannot delete service chain: Unknown service chain ID %s",
                    data.c_str()
                );
            }

            int ret = m->remove_service_chain(sc, errh);
            if (ret == 0) {
                delete(sc);
            }

            return ret;
        }
        case h_delete_controllers: {
            return m->delete_controller_from_json((const String &) data);
        }
    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        case h_rules_from_file: {
            int delim = data.find_left(' ');
            String nic_name = data.substring(0, delim).trim_space_left();
            String filename = data.substring(delim + 1).trim_space_left();

            NIC *nic = m->get_nic_by_name(nic_name);
            if (!nic) {
                return errh->error("Invalid NIC %s", nic_name.c_str());
            }
            FromDPDKDevice *fd = dynamic_cast<FromDPDKDevice *>(nic->element);
            if (!fd) {
                return errh->error("Invalid NIC %s", nic_name.c_str());
            }
            portid_t port_id = fd->get_device()->get_port_id();

            click_chatter("[NIC %d] Rule installation from file: %s", port_id, filename.c_str());

            struct Metron::rule_timing_stats rits;
            rits.start = Timestamp::now_steady();

            int status = FlowDirector::get_flow_director(port_id)->add_rules_from_file(filename);

            rits.end = Timestamp::now_steady();

            uint32_t installed_rules = FlowDirector::get_flow_director(port_id)->flow_rules_count_explicit();

            rits.rules_nb = (uint32_t) installed_rules;
            rits.rules_per_sec = (float) ((rits.end - rits.start).msecval() * 1000) /
                                 (float) installed_rules;
            Metron::add_rule_inst_stats(rits);

            if (m->_verbose) {
                click_chatter(
                    "Installed %" PRId32 " rules at the rate of %.3f rules/sec",
                    installed_rules, rits.rules_per_sec
                );
            }

            return status;
        }
        case h_delete_rules: {
            click_chatter("Metron controller requested rule deletion");

            struct Metron::rule_timing_stats rdts;
            rdts.start = Timestamp::now_steady();

            int32_t deleted_rules = ServiceChain::delete_rule_batch_from_json(data, m, errh);
            if (deleted_rules < 0) {
                return ERROR;
            }

            rdts.end = Timestamp::now_steady();

            // Divide by the number of deleted rules to calculate the rule deletion rate
            rdts.rules_nb = (uint32_t) deleted_rules;
            rdts.rules_per_sec = (float) ((rdts.end - rdts.start).msecval() * 1000) /
                                 (float) deleted_rules;
            Metron::add_rule_del_stats(rdts);

            if (m->_verbose) {
                click_chatter(
                    "Deleted %" PRId32 " rules at the rate of %.3f rules/sec",
                    deleted_rules, rdts.rules_per_sec
                );
            }

            return SUCCESS;
        }
        case h_delete_rules_all: {
            click_chatter("Metron controller requested rules flush");

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
            case h_chains: {
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
            case h_chains_stats: {
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
        #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
            case h_chains_rules: {
                if (param == "") {
                    click_chatter("Metron controller requested local rules for all service chains");

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
                        "Metron controller requested local rules for service chain %s",
                        sc->get_id().c_str()
                    );
                    jroot = sc->rules_to_json();
                }
                break;
            }
        #endif
            case h_chains_proxy: {
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
                param = sc->simple_call_read(param.substring(pos + 1));
                return SUCCESS;
            }
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
            case h_chains: {
                Json jroot = Json::parse(param);
                Json jlist = jroot.get("serviceChains");
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
            case h_put_chains: {
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
        #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
            case h_chains_rules: {
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

                    struct Metron::rule_timing_stats rits;
                    rits.start = Timestamp::now_steady();

                    // Parse
                    int32_t installed_rules =  sc->rules_from_json(jsc.second, m, errh);
                    if (installed_rules < 0) {
                        return errh->error(
                            "Cannot install NIC rules for service chain %s: Parse error",
                            sc_id.c_str()
                        );
                    }

                    rits.end = Timestamp::now_steady();

                    rits.rules_nb = (uint32_t) installed_rules;
                    rits.rules_per_sec = (float) ((rits.end - rits.start).msecval() * 1000) /
                                         (float) installed_rules;
                    Metron::add_rule_inst_stats(rits);

                    if (sc->_verbose) {
                        click_chatter(
                            "Installed %" PRId32 " rules at the rate of %.3f rules/sec",
                            installed_rules, rits.rules_per_sec
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

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
/**
 * Metron agent's rule statistics handlers.
 */
String
Metron::rule_stats_handler(Element *e, void *user_data)
{
    Metron *m = static_cast<Metron *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    float min = std::numeric_limits<float>::max();
    float avg = 0;
    float max = 0;

    switch (what) {
        case h_rule_inst_min:
        case h_rule_inst_avg:
        case h_rule_inst_max: {
            m->min_avg_max(Metron::_rule_inst_stats_map, min, avg, max);
            if ((intptr_t) what == h_rule_inst_min) {
                return String(min);
            } else if ((intptr_t) what == h_rule_inst_avg) {
                return String(avg);
            } else {
                return String(max);
            }
        }
        case h_rule_del_min:
        case h_rule_del_avg:
        case h_rule_del_max: {
            m->min_avg_max(Metron::_rule_del_stats_map, min, avg, max);
            if ((intptr_t) what == h_rule_del_min) {
                return String(min);
            } else if ((intptr_t) what == h_rule_del_avg) {
                return String(avg);
            } else {
                return String(max);
            }
        }
        default: {
            click_chatter("Unknown rule statistics handler: %d", what);
            return "";
        }
    }

    return "";
}
#endif

/**
 * Creates Metron agent's handlers.
 */
void
Metron::add_handlers()
{
    // HTTP get handlers
    add_read_handler ("discovered",  read_handler,  h_discovered);
    add_read_handler ("resources",   read_handler,  h_resources);
    add_read_handler ("controllers", read_handler,  h_controllers);
    add_read_handler ("stats",       read_handler,  h_stats);
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    add_read_handler ("rule_installation_rate_min", rule_stats_handler, h_rule_inst_min);
    add_read_handler ("rule_installation_rate_avg", rule_stats_handler, h_rule_inst_avg);
    add_read_handler ("rule_installation_rate_max", rule_stats_handler, h_rule_inst_max);

    add_read_handler ("rule_deletion_rate_min", rule_stats_handler, h_rule_del_min);
    add_read_handler ("rule_deletion_rate_avg", rule_stats_handler, h_rule_del_avg);
    add_read_handler ("rule_deletion_rate_max", rule_stats_handler, h_rule_del_max);
#endif

    // HTTP post handlers
    add_write_handler("controllers",     write_handler, h_controllers);
    add_write_handler("rules_from_file", write_handler, h_rules_from_file);

    // Get and POST HTTP handlers with parameters
    set_handler(
        "put_chains",
        Handler::f_write,
        param_handler, h_put_chains, h_put_chains);

    set_handler(
        "chains",
        Handler::f_write | Handler::f_read | Handler::f_read_param,
        param_handler, h_chains, h_chains
    );
    set_handler(
        "chains_stats", Handler::f_read | Handler::f_read_param,
        param_handler, h_chains_stats
    );
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    set_handler(
        "rules", Handler::f_write | Handler::f_read | Handler::f_read_param,
        param_handler, h_chains_rules, h_chains_rules
    );
#endif
    set_handler(
        "chains_proxy", Handler::f_read | Handler::f_read_param,
        param_handler, h_chains_proxy
    );

    // HTTP delete handlers
    add_write_handler("delete_chains",      write_handler, h_delete_chains);
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    add_write_handler("delete_rules",       write_handler, h_delete_rules);
    add_write_handler("delete_rules_all",   write_handler, h_delete_rules_all);
#endif
    add_write_handler("delete_controllers", write_handler, h_delete_controllers);
}

/**
 * Encodes hardware information to JSON.
 */
void
Metron::hw_info_to_json(Json &j)
{
    j.set("manufacturer", Json(_cpu_vendor));
    j.set("hwVersion", Json(_hw));
    j.set("swVersion", Json("Click " + _sw));
}

/**
 * Encodes Metron resources to JSON.
 */
Json
Metron::to_json()
{
    Json jroot = Json::make_object();

    jroot.set("id", Json(_id));
    jroot.set("serial", Json(_serial));

    // Info
    hw_info_to_json(jroot);

    // CPU resources
    Json jcpus = Json::make_array();
    for (int i = 0; i < get_cpus_nb(); i++) {
        Json jcpu = Json::make_object();
        jcpu.set("id", i);
        jcpu.set("vendor", _cpu_vendor);
        jcpu.set("frequency", cycles_hz() / CPU::MEGA_HZ);  // In MHz
        jcpus.push_back(jcpu);
    }
    jroot.set("cpus", jcpus);

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

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
/**
 * Computes the minimum, average, and maximum rule
 * installation/deletion rate across the entire set of
 * such operations.
 */
void
Metron::min_avg_max(
    HashMap<uint32_t, struct rule_timing_stats> &rule_stats_map, float &min, float &mean, float &max)
{
    auto it = rule_stats_map.begin();
    int len = rule_stats_map.size();

    float sum = 0.0;

    while (it != rule_stats_map.end()) {
        struct rule_timing_stats stats = it.value();

        float rate = stats.rules_per_sec;
        if (rate < min) {
            min = rate;
        }
        if (rate > max) {
            max = rate;
        }
        sum += rate;

        it++;
    }

    // Set minimum properly if not updated above
    if (min == std::numeric_limits<float>::max()) {
        min = 0;
    }

    if (len == 0) {
        mean = 0;
    } else {
        mean = sum / static_cast<float>(len);
    }
}

/**
 * Flushes all rules from all Metron NICs.
 */
int
Metron::flush_nics()
{
    auto it = _nics.begin();

    while (it != _nics.end()) {
        NIC *nic = &it.value();

        struct Metron::rule_timing_stats rdts;
        rdts.start = Timestamp::now_steady();

        uint32_t nic_rules_nb = 0;

        // Skip empty NICs
        if ((nic_rules_nb = nic->flush_rules()) == 0) {
            it++;
            continue;
        }

        rdts.end = Timestamp::now_steady();

        rdts.rules_nb = nic_rules_nb;
        rdts.rules_per_sec = (float) ((rdts.end - rdts.start).msecval() * 1000) /
                             (float) nic_rules_nb;
        Metron::add_rule_del_stats(rdts);

        if (_verbose) {
            click_chatter(
                "[NIC %s] Deleted %" PRId32 " rules at the rate of %.3f rules/sec",
                nic->get_device_address().c_str(), nic_rules_nb, rdts.rules_per_sec
            );
        }

        it++;
    }

    return SUCCESS;
}
#endif

/**
 * Initializes CPU information for a service chain.
 */
void
ServiceChain::initialize_cpus(int initial_cpu_nb, int max_cpu_nb)
{
    _initial_cpus_nb = initial_cpu_nb;
    _max_cpus_nb = max_cpu_nb;
    _autoscale = false;
    _cpus.resize(max_cpu_nb,CpuInfo());
    for (int i = 0; i < max_cpu_nb; i++) {
        _cpus[i].cpu_phys_id = -1;
    }
}

/**
 * Returns statistics of a CPU core as a JSON object.
 */
Json
ServiceChain::get_cpu_stats(int j)
{
    ServiceChain *sc = this;
    CpuInfo &cpu = sc->get_cpu_info(j);
    int cpu_id = sc->get_cpu_phys_id(j);
    Json jcpu = Json::make_object();
    jcpu.set("id", cpu_id);
    jcpu.set("queue", cpu.max_nic_queue);
    jcpu.set("load", cpu.load);
    jcpu.set("busy", cpu.active_since());

    // Additional per-core statistics in monitoring mode
    if (sc->_metron->_monitoring_mode) {
        LatencyInfo lat = sc->get_cpu_info(j).latency;
        sc->_metron->add_per_core_monitoring_data(&jcpu, lat);
    }

    return jcpu;
}

/**
 * Encodes global Metron statistics to JSON.
 */
Json
Metron::stats_to_json()
{
    Json jroot = Json::make_object();

    // No controller
    if (!_discovered) {
        click_chatter(
            "Cannot report global statistics: Metron agent is not associated with a controller"
        );
        return jroot;
    }

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

        for (int j = 0; j < sc->get_max_cpu_nb(); j++) {
            Json jcpu = sc->get_cpu_stats(j);
            jcpus.push_back(jcpu);

            assigned_cpus++;
            busy_cpus.push_back(sc->get_cpu_phys_id(j));
        }

        sci++;
    }

    // Now, inititialize the load of each idle core to 0
    for (int j = 0; j < get_cpus_nb(); j++) {
        int *found = find(busy_cpus.begin(), busy_cpus.end(), j);
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
            add_per_core_monitoring_data(&jcpu, LatencyInfo());
        }

        jcpus.push_back(jcpu);
    }

    /*
     * At this point the JSON array should have load
     * information for each core of this server.
     */
    assert(jcpus.size() == get_cpus_nb());
    assert(assigned_cpus == get_assigned_cpus_nb());

    jroot.set("cpus", jcpus);

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

/**
 * Extends the input JSON object with additional fields.
 * These fields contain per-core measurements, such as
 * average throughput and several latency percentiles.
 */
void
Metron::add_per_core_monitoring_data(Json *jobj, const LatencyInfo &lat)
{
    if (!jobj) {
        click_chatter("Input JSON object is NULL. Cannot add per-core monitoring data");
        return;
    }

    if ((lat.avg_throughput < 0) || (lat.min_latency < 0) ||
        (lat.average_latency < 0) || (lat.max_latency < 0)) {
        click_chatter("Invalid per-core monitoring data");
        return;
    }

    Json jtput = Json::make_object();
    jtput.set("average", lat.avg_throughput);
    jtput.set("unit", "bps");
    jobj->set("throughput", jtput);

    Json jlat = Json::make_object();
    jlat.set("min", lat.min_latency);
    jlat.set("average", lat.average_latency);
    jlat.set("max", lat.max_latency);
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
    // A list of controllers is expected
    Json jlist = j.get("controllers");

    for (unsigned short i=0; i<jlist.size(); i++) {
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
 * Disassociates this Metron agent from a Metron controller.
 */
int
Metron::delete_controller_from_json(const String &ip)
{
    // This agent is not associated with a Metron controller at the moment
    if (_discover_ip.empty()) {
        click_chatter("No controller associated with this Metron agent");
        return ERROR;
    }

    // Request to remove a controller must have correct IP
    if ((!ip.empty()) && (ip != _discover_ip)) {
        click_chatter("Metron agent is not associated with a Metron controller on %s", ip.c_str());
        return ERROR;
    }

    click_chatter(
        "Metron controller instance on %s:%d has been removed",
        _discover_ip.c_str(), _discover_port
    );

    // Reset controller information
    _discovered    = false;
    _discover_ip   = "";
    _discover_port = -1;

    return SUCCESS;
}

/***************************************
 * RxFilter
 **************************************/
ServiceChain::RxFilter::RxFilter(ServiceChain *sc) : sc(sc)
{

}

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
    for (int nic_id = 0; nic_id < sc->get_nics_nb(); nic_id++) {
        NIC *nic = sc->get_nic_by_index(nic_id);
        Json jaddrs = Json::make_array();
        for (int j = 0; j < sc->get_max_cpu_nb(); j++) {
            const int core_id = sc->get_cpu_phys_id(j);
            const String tag = get_tag_value(nic_id, core_id);
            assert(!tag.empty());
            jaddrs.push_back(tag);
        }
        jnic_values.set(nic->get_name(), jaddrs);
    }
    j.set("values", jnic_values);

    return j;
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

    String method_str = rx_filter_type_enum_to_str(method).c_str();
    click_chatter("Rx filters in mode: %s", method_str.upper().c_str());

    if (method == MAC) {
        Json jaddrs = Json::parse(nic->call_rx_read("vf_mac_addr"));

        for (int i = 0; i < sc->get_max_cpu_nb(); i++) {
            int available_pools = atoi(nic->call_rx_read("nb_vf_pools").c_str());
            const int core_id = i;
            if (available_pools <= core_id) {
                return errh->error("Not enough VF pools: %d are available", available_pools);
            }
            set_tag_value(inic, core_id, jaddrs[core_id].to_s());
        }
    } else if ((method == FLOW) || (method == RSS)) {
        // Advertize the available CPU core IDs
        for (int i = 0; i < sc->get_max_cpu_nb(); i++) {
            const int core_id = i;
            set_tag_value(inic, core_id, String(core_id));
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
ServiceChain::ServiceChain(Metron *m)
    : id(), rx_filter(0), config(), config_type(UNKNOWN),
      _metron(m), _nics(), _cpus(), _nic_stats(),
      _initial_cpus_nb(0), _max_cpus_nb(0),
      _total_cpu_load(0), _max_cpu_load(0), _max_cpu_load_index(0),
      _socket(-1), _pid(-1), _timing_stats(), _as_timing_stats(),
      _autoscale(false), _last_autoscale()
{
    _verbose = m->_verbose;
}

ServiceChain::~ServiceChain()
{
    if (rx_filter) {
        delete rx_filter;
    }

    _cpus.clear();
    _nic_stats.clear();
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
        errh->error("Max number of CPUs must be greater than the number of used CPUs");
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
            errh->warning("NICs should be a JSON array. Assuming you passed one NIC named %s...", jnics.c_str());
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

    sc->_nic_stats.resize(sc->_nics.size() * sc->_max_cpus_nb,NicStat());
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
    jsc.set("expandedConfig", generate_configuration());
    Json jcpus = Json::make_array();
    for (int i = 0; i < get_max_cpu_nb(); i++) {
        jcpus.push_back(i); //TODO :: phys ids ?
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
    for (int j = 0; j < get_max_cpu_nb(); j++) {
        String js = String(j);
/*        int avg_max = 0;
          for (int i = 0; i < get_nics_nb(); i++) {
            String is = String(i);
            int avg = atoi(
                simple_call_read("batchAvg" + is + "C" + js + ".average").c_str()
            );
            if (avg > avg_max)
                avg_max = avg;
        }*/

        Json jcpu = get_cpu_stats(j);
        jcpus.push_back(jcpu);
    }
    jsc.set("cpus", jcpus);

    Json jnics = Json::make_array();
    for (int i = 0; i < get_nics_nb(); i++) {
        String is = String(i);
        uint64_t rx_count   = 0;
        uint64_t rx_bytes   = 0;
        uint64_t rx_dropped = 0;
        uint64_t rx_errors  = 0;
        uint64_t tx_count   = 0;
        uint64_t tx_bytes   = 0;
        uint64_t tx_dropped = 0;
        uint64_t tx_errors  = 0;

        for (int j = 0; j < get_max_cpu_nb(); j++) {
            String js = String(j);
            rx_count += atol(
                simple_call_read("slaveFD" + is + "C" + js + ".count").c_str()
            );
            // rx_bytes += atol(
            //     simple_call_read( "slaveFD" + is + "C" + js + ".bytes").c_str()
            // );
            rx_dropped += atol(
                simple_call_read("slaveFD" + is + "C" + js + ".dropped").c_str()
            );
            // rx_errors += atol(
            //     simple_call_read( "slaveFD" + is + "C" + js + ".errors").c_str()
            // );

        }
        tx_count += atol(
            simple_call_read("slaveTD" + is + ".count").c_str()
        );
        // tx_bytes += atol(
        //     simple_call_read("slaveTD" + is + ".bytes").c_str()
        // );
        tx_dropped += atol(
            simple_call_read("slaveTD" + is + ".dropped").c_str()
        );
        // tx_errors += atol(
        //     simple_call_read("slaveTD" + is + ".errors").c_str()
        // );

        Json jnic = Json::make_object();
        jnic.set("name",      get_nic_by_index(i)->get_name());
        jnic.set("index",     get_nic_by_index(i)->get_index());
        jnic.set("rxCount",   rx_count);
        jnic.set("rxBytes",   rx_bytes);
        jnic.set("rxDropped", rx_dropped);
        jnic.set("rxErrors",  rx_errors);
        jnic.set("txCount",   tx_count);
        jnic.set("txBytes",   tx_bytes);
        jnic.set("txDropped", tx_dropped);
        jnic.set("txErrors",  tx_errors);
        jnics.push_back(jnic);
    }
    jsc.set("nics", jnics);

    jsc.set("timingStats", _timing_stats.to_json());
    jsc.set("autoScaleTimingStats", _as_timing_stats.to_json());

    return jsc;
}

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
/**
 * Decodes service chain rules from JSON
 * and installs the rules in the respective NIC.
 * Returns the number of installed rules on success,
 * otherwise a negative integer.
 */
int32_t
ServiceChain::rules_from_json(Json j, Metron *m, ErrorHandler *errh)
{
    // No controller
    if (!m->_discovered) {
        errh->error(
            "Cannot reconfigure service chain %s: Metron agent is not associated with a controller",
            get_id().c_str()
        );
        return (int32_t) ERROR;
    }

    RxFilterType rx_filter_type = rx_filter_type_str_to_enum(j.get("rxFilter").get_s("method").upper());
    if (rx_filter_type != FLOW) {
        errh->error(
            "Cannot install rules for service chain %s: "
            "Invalid Rx filter mode %s is sent by the controller.",
            get_id().c_str(),
            rx_filter_type_enum_to_str(rx_filter_type).c_str()
        );
        return (int32_t) ERROR;
    }

    uint32_t rules_nb = 0;
    uint32_t inserted_rules_nb = 0;

    Json jnics = j.get("nics");
    int inic = 0;
    for (auto jnic : jnics) {
        String nic_name = jnic.second.get_s("nicName");

        Json jcpus = jnic.second.get("cpus");
        for (auto jcpu : jcpus) {
            int core_id = jcpu.second.get_i("cpuId");
            click_chatter("Adding rules for CPU %d", core_id);
            assert(get_cpu_info(core_id).active());

            Json jrules = jcpu.second.get("cpuRules");
            for (auto jrule : jrules) {
                long rule_id = jrule.second.get_i("ruleId");
                String rule = jrule.second.get_s("ruleContent");

                // Get the correct NIC
                NIC *nic = this->get_nic_by_name(nic_name);
                if (!nic) {
                    return (int32_t) errh->error(
                        "Metron controller attempted to install rules in unknown NIC: %s",
                        nic_name.c_str()
                    );
                }

                // Update the data plane with this new rule
                if (!nic->update_rule(core_id, rule_id, rule)) {
                    errh->error(
                        "Unable to insert rule %ld into NIC %s mapped to CPU core %d",
                        rule_id, nic_name.c_str(), core_id
                    );
                } else {
                    inserted_rules_nb++;
                }
                rules_nb++;

                // Add this tag to the list of tags of this NIC
                if (!this->rx_filter->has_tag_value(inic, core_id)) {
                    this->rx_filter->set_tag_value(inic, core_id, String(core_id));
                }
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
    for (int i = 0; i < get_nics_nb(); i++) {
        NIC *nic = _nics[i];

        Json jnic = Json::make_object();

        jnic.set("nicName", nic->get_name());

        Json jcpus_array = Json::make_array();

        // One NIC can dispatch to multiple CPU cores
        for (int j = 0; j < get_max_cpu_nb(); j++) {
            // Fetch the rules for this NIC and this CPU core
            HashMap<long, String> *rules_map = nic->find_rules_by_core_id(j);
            if (!rules_map || rules_map->empty()) {
                continue;
            }

            Json jcpu = Json::make_object();
            jcpu.set("cpuId", j);

            Json jrules = Json::make_array();

            auto begin = rules_map->begin();
            while (begin != rules_map->end()) {
                long rule_id = begin.key();
                String rule = begin.value();

                Json jrule = Json::make_object();
                jrule.set("ruleId", rule_id);
                jrule.set("ruleContent", rule);
                jrules.push_back(jrule);

                begin++;
            }

            jcpu.set("cpuRules", jrules);

            jcpus_array.push_back(jcpu);
        }

        jnic.set("cpus", jcpus_array);

        jnics_array.push_back(jnic);
    }

    jsc.set("nics", jnics_array);

    return jsc;
}

/**
 * Decodes service chain rule from JSON
 * and deletes the rule from the respective NIC.
 */
int
ServiceChain::delete_rule_from_json(const long &rule_id, Metron *m, ErrorHandler *errh)
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
    auto n = m->_nics.begin();
    while (n != m->_nics.end()) {
        NIC *nic = &n.value();

        if (!nic->has_rules()) {
            n++;
            continue;
        }

        // Attempt to remove
        if (nic->remove_rule(rule_id)) {
            return SUCCESS;
        }

        n++;
    }

    return ERROR;
}

/**
 * Decodes a batch of service chain rules from JSON
 * and deletes these rules from the respective NIC.
 * Returns the number of successfully deleted rules on success,
 * otherwise a negative integer.
 */
int32_t
ServiceChain::delete_rule_batch_from_json(String rule_ids, Metron *m, ErrorHandler *errh)
{
    // No controller
    if (!m->_discovered) {
        errh->error(
            "Cannot delete rule batch %s: Metron agent is not associated with a controller",
            rule_ids.c_str()
        );
        return (int32_t) ERROR;
    }

    int status = SUCCESS;
    int current = 0;
    int pos = -1;
    uint32_t deleted_rules = 0;
    uint32_t all_rules = 0;
    while ((pos = rule_ids.find_left(',')) >= 0) {
        // Keep a rule ID until the comma
        const String rule_id_str = rule_ids.substring(0, pos);
        const long rule_id = atol(rule_id_str.c_str());

        // Delete this rule
        status += delete_rule_from_json(rule_id, m, errh);
        if (status == SUCCESS) {
            deleted_rules++;
        }
        all_rules++;

        // Advance to the next rule ID
        current = pos + 1;
        rule_ids = rule_ids.substring(current, rule_ids.length() - 1);
    }

    // Last (or single) rule left
    const long rule_id = atol(rule_ids.c_str());
    status += delete_rule_from_json(rule_id, m, errh);
    if (status == SUCCESS) {
        deleted_rules++;
    }
    all_rules++;

    click_chatter("Successfully deleted %d/%d NIC rules", deleted_rules, all_rules);

    return (status == 0) ? (int32_t) deleted_rules : (int32_t) ERROR;
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

            for (int i = 0; jfield.second.size(); i++) {
                if (jfield.second[i].as_i() >  get_max_cpu_nb()) {
                    return errh->error(
                        "Number of used CPUs must be less or equal "
                        "than the maximum number of CPUs!"
                    );
                }
                new_map[jfield.second[i].as_i()] = true;
            }

            int ret;
            String response = "";
            bool did_scale = false;

            if (_metron->_rx_mode == RSS) {
                for (int i = 0; i < new_map.size(); i++) {
                    if (!new_map[i] && i < new_map.weight()) {
                        return errh->error("RSS must allocate CPUs without holes !");
                    }
                }
            }

            for (int new_cpu_id = 0; new_cpu_id < get_max_cpu_nb(); new_cpu_id++) {
                if (old_map[new_cpu_id] == new_map[new_cpu_id]) {
                    continue;
                }

                did_scale = true;

                // Activating core
                get_cpu_info(new_cpu_id).set_active(new_map[new_cpu_id]);
                if (new_map[new_cpu_id]) {
                    for (int inic = 0; inic < get_nics_nb(); inic++) {
                        ret = call_write(
                            generate_configuration_slave_fd_name(
                                inic, get_cpu_phys_id(new_cpu_id)
                            ) + ".safe_active", response, "1"
                        );
                        if ((ret < 200) || (ret >= 300)) {
                            return errh->error(
                                "Response to activate input was %d: %s",
                                ret, response.c_str()
                            );
                        }
                        click_chatter("Response %d: %s", ret, response.c_str());
                        // Actually use the new cores AFTER the secondary has been advertised
                        if (_metron->_rx_mode == RSS) {
                            _nics[inic]->call_rx_write("max_rss", String(new_cpu_id + 1));
                        }
                    }
                // Scale down
                } else {
                    for (int inic = 0; inic < get_nics_nb(); inic++) {
                        // Stop using the new cores BEFORE the secondary has been torn down
                        if (_metron->_rx_mode == RSS) {
                            _nics[inic]->call_rx_write("max_rss", String(new_cpu_id + 1));
                        }
                        int ret = call_write(
                            generate_configuration_slave_fd_name(
                                inic, get_cpu_phys_id(new_cpu_id)
                            ) + ".safe_active", response, "0"
                        );
                        if ((ret < 200) || (ret >= 300)) {
                            return errh->error(
                                "Response to activate input was %d: %s",
                                ret, response.c_str()
                            );
                        }
                    }
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
    for (int i = 0; i < abs(n_cpu_change); i++) {
        get_cpu_info(last_idx + (n_cpu_change>0?i:-1)).set_active(n_cpu_change > 0);
    }
    click_chatter(
        "Autoscale: Service chain %s uses %d CPU(s)",
        this->get_id().c_str(), get_active_cpu_nb()
    );

    String response;
    int ret = call_write("slave/rrs.max", response, String(get_active_cpu_nb()));
    if ((ret < 200) || (ret >= 300)) {
        errh->error(
            "Response to change the number of CPU core %d: %s",
            ret, response.c_str()
        );
        return;
    }

    // Measure again
    ts.autoscale_end = Timestamp::now_steady();
    click_chatter(
        "Autoscale: Duration %d nsec",
        (ts.autoscale_end - ts.autoscale_start).nsecval()
    );
    this->set_autoscale_timing_stats(ts);
}

/**
 * Generates the software configuration for a given service chain
 * as received by the controller.
 */
String
ServiceChain::generate_configuration()
{
    String new_conf = "elementclass MetronSlave {\n" + config + "\n};\n\n";
    if (_autoscale) {
        new_conf += "slave :: {\n";

        new_conf += "rrs :: RoundRobinSwitch(MAX " + String(get_active_cpu_nb()) + ");\n";
        new_conf += "ps :: PaintSwitch();\n\n";

        for (int i = 0 ; i < get_max_cpu_nb(); i++) {
            new_conf += "rrs[" + String(i) + "] -> slavep" + String(i) +
                       " :: Pipeliner(CAPACITY 8, BLOCKING false) -> "
                       "[0]ps; StaticThreadSched(slavep" +
                       String(i) + " " + String(get_cpu_phys_id(i)) + ");\n";
        }
        new_conf += "\n";

        for (int i = 0; i < get_nics_nb(); i++) {
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

    // NICs require an additional parameter if in Flow Director mode
    if (get_rx_mode() == FLOW) {
        rx_conf += "MODE flow_dir, ";
    }

    for (int i = 0; i < get_nics_nb(); i++) {
        String is = String(i);
        NIC *nic = get_nic_by_index(i);
        if (_metron->_rx_mode == RSS) {
            nic->call_rx_write("max_rss", String(get_active_cpu_nb()));
        }

        for (int j = 0; j < get_max_cpu_nb(); j++) {
            assert(get_cpu_info(j).assigned());
            String js = String(j);
            String active = (j < get_cpu_info(j).active() ? "1":"0");
            int phys_cpu_id = get_cpu_phys_id(j);
            int queue_no = rx_filter->phys_cpu_to_queue(nic, phys_cpu_id);
            String ename = generate_configuration_slave_fd_name(i, j);
            new_conf += ename + " :: " + nic->element->class_name() +
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
        assert(get_cpu_info(0).assigned());
            int phys_cpu_id = get_cpu_phys_id(0);
            int queue_no = rx_filter->phys_cpu_to_queue(nic, phys_cpu_id);
            new_conf += "slaveTD" + is + " :: ToDPDKDevice(" + nic->get_device_address() + ", QUEUE " + String(queue_no) + ", VERBOSE 99);\n";
        } else {
            new_conf += "slaveTD" + is + " :: ExactCPUSwitch();\n";
            for (int j = 0; j < get_max_cpu_nb(); j++) {
                String js = String(j);
                assert(get_cpu_info(j).assigned());
                int phys_cpu_id = get_cpu_phys_id(j);
                String ename = generate_configuration_slave_fd_name(i, j, "TD");
                int queue_no = rx_filter->phys_cpu_to_queue(nic, phys_cpu_id);
                new_conf += ename + " :: ToDPDKDevice(" + nic->get_device_address() + ", QUEUE " + String(queue_no) + ", VERBOSE 99, MAXQUEUES 1);";

                new_conf += "slaveTD" + is + "["+js+"] -> " + ename + ";\n";
            }
        }
        new_conf += "slave[" + is + "] -> " + (_metron->_monitoring_mode ? "[" + is + "]monitoring_lat[" + is + "] -> " : "") + "  slaveTD" + is + ";\n\n";
    }

    new_conf += _metron->_slave_extra;

    return new_conf;
}

/**
 * Generates the necessary DPDK arguments for the deployment
 * of a service chain as a secondary DPDK process.
 */
Vector<String>
ServiceChain::build_cmd_line(int socketfd)
{
    int i;
    Vector<String> argv;

    String cpu_list = "";

    for (i = 0; i < click_max_cpu_ids(); i++) {
        cpu_list += String(i) + (i < click_max_cpu_ids() -1? "," : "");
    }

    argv.push_back(click_path);
    argv.push_back("--dpdk");
    argv.push_back("-l");
    argv.push_back(cpu_list);
    argv.push_back("--proc-type=secondary");

    for (i = 0; i < _metron->_dpdk_args.size(); i++) {
        argv.push_back(_metron->_dpdk_args[i]);
    }
    argv.push_back("--");
    argv.push_back("--socket");
    argv.push_back(String(socketfd));
    for (i = 0; i < _metron->_args.size(); i++) {
        argv.push_back(_metron->_args[i]);
    }

    for (i = 0; i < argv.size(); i++)  {
        click_chatter("ARG %s", argv[i].c_str());
    }
    return argv;
}

/**
 * Returns a bit map with the CPU core assignment of a service chain.
 */
Bitvector
ServiceChain::active_cpus()
{
    Bitvector b;
    b.resize(get_max_cpu_nb());
    for (int i = 0; i < b.size(); i++) {
        b[i] = _cpus[i].active();
    }
    return b;
}


/**
 * Checks whether a service chain is alive or not.
 */
void
ServiceChain::check_alive()
{
    if (kill(_pid, 0) != 0) {
        _metron->remove_service_chain(this, ErrorHandler::default_handler());
    } else {
        click_chatter("Error: PID %d is still alive", _pid);
    }
}

/**
 * Initializes the control socket of a service chain.
 */
void
ServiceChain::control_init(int fd, int pid)
{
    _socket = fd;
    _pid = pid;
}

/**
 * Reads a control message from the control socket of a service chain.
 */
int
ServiceChain::control_read_line(String &line)
{
    char buf[1024];
    int n = read(_socket, &buf, 1024);
    if (n <= 0) {
        return n;
    }

    line = String(buf, n);
    while (n == 1024) {
        n = read(_socket, &buf, 1024);
        line += String(buf, n);
    }

    return line.length();
}

/**
 * Writes a control message to the control socket of a service chain.
 */
void
ServiceChain::control_write_line(String cmd)
{
    int n = write(_socket, (cmd + "\r\n").c_str(),cmd.length() + 1);
}

/**
 * Passes a control message through the control socket and gets
 * a response.
 */
String
ServiceChain::control_send_command(String cmd)
{
    control_write_line(cmd);
    String ret;
    control_read_line(ret);

    return ret;
}

/**
 * Implements handlers for a service chain using the control socket.
 */
int
ServiceChain::call(
        String fnt, bool has_response, String handler,
        String &response, String params)
{
    _metron->_command_lock.acquire();
    String ret = control_send_command(fnt + " " + handler + (params? " " + params : ""));
    if (ret.empty()) {
        check_alive();
        _metron->_command_lock.release();
        return ERROR;
    }

    int code = atoi(ret.substring(0, 3).c_str());
    if (code >= 500) {
        response = ret.substring(4);
        _metron->_command_lock.release();
        return code;
    }
    if (has_response) {
        ret = ret.substring(ret.find_left("\r\n") + 2);
        if (!ret.starts_with("DATA ")) {
            click_chatter("Got answer '%s' (code %d), that does not start with DATA !", ret.c_str(), code);
            click_chatter("Command was %s %s %s", fnt.c_str(), handler.c_str(), params ? params.c_str() : "");
            abort();
        }
        ret = ret.substring(5); //Code + return
        int eof = ret.find_left("\r\n");
        int n = atoi(ret.substring(0, eof).c_str()); //Data length
        response = ret.substring(eof + 2, n);
    } else {
        response = ret.substring(4);
    }

    _metron->_command_lock.release();
    return code;
}

/**
 * Implements simple read handlers for a service chain.
 */
String
ServiceChain::simple_call_read(String handler)
{
    String response;

    int code = call("READ", true, handler, response, "");
    if ((code >= 200) && (code < 300)) {
        return response;
    }

    return "";
}

/**
 * Implements simple write handlers for a service chain.
 */
String
ServiceChain::simple_call_write(String handler)
{
    String response;

    int code = call("WRITE", false, handler, response, "");
    if ((code >= 200) && (code < 300)) {
        return response;
    }

    return "";
}

/**
 * Implements read handlers for a service chain.
 */
int
ServiceChain::call_read(String handler, String &response, String params)
{
    return call("READ", true, handler, response, params);
}

/**
 * Implements write handlers for a service chain.
 */
int
ServiceChain::call_write(String handler, String &response, String params)
{
    return call("WRITE", false, handler, response, params);
}

/******************************
 * CPU
 ******************************/
/**
 * Retunrs a CPU ID.
 */
int
CPU::get_id()
{
    return this->_id;
}

/**
 * Retunrs a CPU's vendor name.
 */
String
CPU::get_vendor()
{
    return this->_vendor;
}

/**
 * Retunrs a CPU's frequency in MHz.
 */
long
CPU::get_frequency()
{
    return this->_frequency;
}

/**
 * Encodes CPU information to JSON.
 */
Json
CPU::to_json()
{
    Json cpu = Json::make_object();

    cpu.set("id", get_id());
    cpu.set("vendor", get_vendor());
    cpu.set("frequency", get_frequency());

    return cpu;
}

/******************************
 * NIC
 ******************************/
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
    nic.set("index", get_index());
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
 * Implements read handlers for a NIC.
 */
String
NIC::call_rx_read(String h)
{
    const Handler *hc = Router::handler(element, h);

    if (hc && hc->visible()) {
        return hc->call_read(element, ErrorHandler::default_handler());
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
    ToDPDKDevice *td = dynamic_cast<FromDPDKDevice *>(element)->findOutputElement();
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
    FromDPDKDevice *fd = dynamic_cast<FromDPDKDevice *>(element);
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


/**
 * Return the DPDK port ID of a NIC.
 */
portid_t
NIC::get_port_id()
{
    return is_ghost() ? -1 :
        static_cast<FromDPDKDevice *>(element)->get_device()->get_port_id();
}

/**
 * Return the device's adress suitable to use as a FromDPDKDevice reference.
 * TODO: Actually this is still a port ID, returning the PCI address would be better.
 */
String
NIC::get_device_address()
{
    return String(get_port_id());
}

/**
 * Return the device's name.
 */
String
NIC::get_name()
{
    return is_ghost() ? "" : element->name();
}

/**
 * Return the device's Click port index.
 */
int
NIC::get_index()
{
    return is_ghost() ? -1 : _index;
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
 * NIC rules' management is provided via DPDK's Flow API.
 * This API is avaialable since v17.05.
 */
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
/**
 * Returns a map of rule Is to rules for a given NIC.
 */
HashMap<long, String> *
NIC::find_rules_by_core_id(const int &core_id)
{
    if (core_id < 0) {
        click_chatter("[NIC %s] Unable to find rules: Invalid core ID %d", get_device_address().c_str(), core_id);
        return NULL;
    }

    return _rules.find(core_id);
}

/**
 * Returns a list of rules associated with a NIC.
 */
Vector<String>
NIC::rules_list_by_core_id(const int &core_id)
{
    Vector<String> rules;

    if (core_id < 0) {
        click_chatter("[NIC %s] Unable to find rules: Invalid core ID %d", get_device_address().c_str(), core_id);
        return rules;
    }

    HashMap<long, String> *rules_map = find_rules_by_core_id(core_id);
    if (!rules_map) {
        click_chatter("[NIC %s] No rules associated with CPU core %d", get_device_address().c_str(), core_id);
        return rules;
    }

    auto begin = rules_map->begin();
    while (begin != rules_map->end()) {
        String rule = begin.value();

        if (!rule.empty()) {
            rules.push_back(rule);
        }

        begin++;
    }

    return rules;
}

/**
 * Returns the number of CPU cores that have at least one rule each.
 */
Vector<int>
NIC::cores_with_rules()
{
    Vector<int> cores_with_rules;

    for (int i = 0; i < click_max_cpu_ids(); i++) {
        Vector<String> core_rules = rules_list_by_core_id(i);

        if (core_rules.empty()) {
            continue;
        }

        cores_with_rules[i] = i;
    }

    return cores_with_rules;
}

/**
 * Adds a new rule to a given NIC's local cache.
 */
bool
NIC::insert_rule(const int &core_id, const long &rule_id, String &rule)
{
    if (core_id < 0) {
        click_chatter("[NIC %s] Unable to add rule: Invalid core ID %d", get_device_address().c_str(), core_id);
        return false;
    }

    if (rule_id < 0) {
        click_chatter("[NIC %s] Unable to add rule: Invalid rule ID %ld", get_device_address().c_str(), rule_id);
        return false;
    }

    if (rule.empty()) {
        click_chatter("[NIC %s] Unable to add rule: Empty rule", get_device_address().c_str());
        return false;
    }

    HashMap<long, String> *rules_map = find_rules_by_core_id(core_id);
    if (!rules_map) {
        _rules.insert(core_id, new HashMap<long, String>());
        rules_map = find_rules_by_core_id(core_id);
        assert(rules_map);
    }

    if (!rules_map->insert(rule_id, rule)) {
        click_chatter("[NIC %s] Unable to add rule: Cache failure", get_device_address().c_str());
        return false;
    }

    int status = install_rule(rule_id, rule);
    if (status < 0) {
        return false;
    }

    uint32_t internal_rule_id = static_cast<uint32_t>(status);

    if (_verbose) {
        click_chatter(
            "[NIC %s] Rule %ld added and mapped with NIC rule ID %" PRIu32 " and CPU core %d",
            get_device_address().c_str(), rule_id, internal_rule_id, core_id
        );
    }

    return store_rule_id_mapping(rule_id, internal_rule_id);
}

/**
 * Installs a new rule in a given NIC.
 * Returns a new NIC rule ID or error.
 */
int
NIC::install_rule(const long &rule_id, String &rule)
{
    // Rule needs to be prepended with the command type and port ID
    rule = "flow create " + String(get_port_id()) + " " + rule;

    /**
     * Calls FlowDirector using FromDPDKDevice's flow handler rule_add.
     * Upon successful invocation, Flow Director returns an internal
     * rule ID which needs to be stored if rule deletion is required
     * in the future. Otherwise, negative value is returned to indicate
     * error during rule installation in the NIC.
     */
    int status = call_rx_write(FlowDirector::FLOW_RULE_ADD, rule);
    if (status < 0) {
        click_chatter("[NIC %s] Unable to install rule '%s'", get_device_address().c_str(), rule.c_str());
        return ERROR;
    }

    uint32_t nic_rule_id =  static_cast<uint32_t>(status);
    if (_verbose) {
        click_chatter("[NIC %s] Rule %ld installed with internal ID %" PRIu32, get_device_address().c_str(), rule_id, nic_rule_id);
    }

    return static_cast<int>(nic_rule_id);
}

/**
 * Removes a rule from a given NIC, by using
 * only rule ID as an index.
 */
bool
NIC::remove_rule(const long &rule_id)
{
    if (rule_id < 0) {
        click_chatter("[NIC %s] Unable to remove rule: Invalid rule ID %ld", get_device_address().c_str(), rule_id);
        return false;
    }

    auto begin = _rules.begin();
    while (begin != _rules.end()) {
        int core_id = begin.key();
        HashMap<long, String> *rules_map = begin.value();

        // No rules for this CPU core
        if (!rules_map || rules_map->empty()) {
            begin++;
            continue;
        }

        // Remove rule from the cache and the NIC
        if (rules_map->remove(rule_id)) {
            // Now fetch the mapping of the controller rule ID with the NIC ID
            uint32_t internal_rule_id = get_internal_rule_id(rule_id);
            if (internal_rule_id < 0) {
                click_chatter("[NIC %s] Unable to remove rule %ld: No internal mapping", get_device_address().c_str(), rule_id);
                return false;
            }

            // Delete this rule from the NIC using the NIC ID
            if (call_rx_write(FlowDirector::FLOW_RULE_DEL, String(internal_rule_id)) != SUCCESS) {
                return false;
            }

            // Also, delete the ID mapping for this rule
            if (!delete_rule_id_mapping(rule_id)) {
                return false;
            }

            if (_verbose) {
                click_chatter("[NIC %s] Rule %ld removed from CPU core %d", get_device_address().c_str(), rule_id, core_id);
            }

            return true;
        }

        begin++;
    }

    if (_verbose) {
        click_chatter("[NIC %s] Unable to remove rule %ld", get_device_address().c_str(), rule_id);
    }

    return false;
}

/**
 * Removes a rule from a given NIC, by using
 * both core and rule IDs as indices.
 */
bool
NIC::remove_rule(const int &core_id, const long &rule_id)
{
    if (core_id < 0) {
        click_chatter("[NIC %s] Unable to remove rule: Invalid core ID %d", get_device_address().c_str(), core_id);
        return false;
    }

    if (rule_id < 0) {
        click_chatter("[NIC %s] Unable to remove rule: Invalid rule ID %ld", get_device_address().c_str(), rule_id);
        return false;
    }

    HashMap<long, String> *rules_map = find_rules_by_core_id(core_id);

    // No rules for this CPU core
    if (!rules_map || rules_map->empty()) {
        click_chatter("[NIC %s] Unable to remove rule: Core ID %ld has no rules", get_device_address().c_str(), rule_id);
        return false;
    }

    // Remove rule from the cache and the NIC
    if (rules_map->remove(rule_id)) {
        // Now fetch the mapping of the controller rule ID with the NIC ID
        uint32_t internal_rule_id = get_internal_rule_id(rule_id);
        if (internal_rule_id < 0) {
            return false;
        }

        // Delete this rule from the NIC using the NIC ID
        if (call_rx_write(FlowDirector::FLOW_RULE_DEL, String(internal_rule_id)) != SUCCESS) {
            return false;
        }

        // Also, delete the ID mapping for this rule
        if (!delete_rule_id_mapping(rule_id)) {
            return false;
        }

        if (_verbose) {
            click_chatter("[NIC %s] Rule %ld removed from CPU core %d", get_device_address().c_str(), rule_id, core_id);
        }

        return true;
    }

    if (_verbose) {
        click_chatter("[NIC %s] Unable to remove rule %ld mapped to CPU core %d", get_device_address().c_str(), rule_id, core_id);
    }

    return false;
}

/**
 * Updates a rule using a two-phase commit.
 * First checks if rule exists and removes it.
 * Then, inserts the new rule.
 */
bool
NIC::update_rule(const int &core_id, const long &rule_id, String &rule)
{
    // First try to remove this rule, if it exists
    remove_rule(rule_id);

    // Now, store this rule in this CPU core's local cache and the NIC
    return insert_rule(core_id, rule_id, rule);
}

/**
 * Removes all rules from a given NIC's cache.
 * FlowDirector element undertakes to flush the NIC
 * on exit, thus we omit this step.
 */
bool
NIC::remove_rules()
{
    if (is_ghost()) {
        return false;
    }

    if (!has_rules()) {
        return true;
    }

    long rules_nb = 0;

    auto begin = _rules.begin();
    while (begin != _rules.end()) {
        int core_id = begin.key();
        HashMap<long, String> *rules_map = begin.value();

        if (!rules_map || rules_map->empty()) {
            begin++;
            continue;
        }

        rules_nb += rules_map->size();

        rules_map->clear();
        delete rules_map;

        begin++;
    }

    _rules.clear();
    _internal_rule_map.clear();

    if ((rules_nb > 0) && (_verbose)) {
        click_chatter("[NIC %s] Successfully removed %ld rules from local cache", get_device_address().c_str(), rules_nb);
    }

    return true;
}

/**
 * Removes all rules from a given NIC along with our local cache.
 */
uint32_t
NIC::flush_rules()
{
    // Fetch the total number of rules in this NIC
    uint32_t nic_rules_nb = (uint32_t) atoi(call_rx_read(FlowDirector::FLOW_RULE_COUNT).c_str());

    // Empty NIC
    if (nic_rules_nb == 0) {
        return 0;
    }

    // Flush this NIC
    if (call_rx_write(FlowDirector::FLOW_RULE_FLUSH, "") != SUCCESS) {
        click_chatter("[NIC %s] Failed to flush NIC rules", get_device_address().c_str());
        return 0;
    }

    if (_verbose) {
        click_chatter(
            "[NIC %s] Successfully removed %ld hardware rules", get_device_address().c_str(), nic_rules_nb
        );
    }

    // Flush also our cache
    if (!remove_rules()) {
        click_chatter("[NIC %s] Failed to flush rules from local cache", get_device_address().c_str());
        return 0;
    }

    return nic_rules_nb;
}

/**
 * Keeps a mapping between controller and NIC rule IDs.
 * This method should be called when a new rule is
 * installed by the controller (see method install_rule).
 */
bool
NIC::store_rule_id_mapping(const long &rule_id, const uint32_t &int_rule_id)
{
    if (rule_id < 0) {
        click_chatter("[NIC %s] Unable to store mapping for invalid controller rule ID %ld", get_device_address().c_str(), rule_id);
        return false;
    }

    /**
     * Verify the uniqueness of this internal ID.
     * TODO: The complexity of this method is O(#OfRules)!
     * Consider removing this operation, thus assuming that the
     * controller knows what it is doing.
     */
    if (!verify_unique_rule_id_mapping(int_rule_id)) {
        return false;
    }

    if (_internal_rule_map.insert(rule_id, int_rule_id)) {
        if (_verbose) {
            click_chatter("[NIC %s] Successfully inserted rule mapping %ld <--> %" PRIu32, get_device_address().c_str(), rule_id, int_rule_id);
        }

        return true;
    }

    return false;
}

/**
 * Traverses the map of controller rule IDs to internal NIC IDs,
 * and verifies that there is no internal rule ID with the
 * input value.
 * This ensures that a future store operation will create a
 * unique mapping.
 */
bool
NIC::verify_unique_rule_id_mapping(const uint32_t &int_rule_id)
{
    if (int_rule_id < 0) {
        click_chatter("[NIC %s] Unable to verify mapping for invalid NIC rule ID %" PRIu32, get_device_address().c_str(), int_rule_id);
        return false;
    }

    auto begin = _internal_rule_map.begin();
    while (begin != _internal_rule_map.end()) {
        uint32_t r_id = begin.value();

        if (r_id == int_rule_id) {
            click_chatter("[NIC %s] Internal rule ID %" PRIu32 " already exists in rule map", get_device_address().c_str(), int_rule_id);
            return false;
        }

        begin++;
    }

    return true;
}

/**
 * Deletes a mapping between a controller and a NIC rule ID.
 * This method should be called when an existing rule is
 * deleted by the controller (see method remove_rule).
 */
bool
NIC::delete_rule_id_mapping(const long &rule_id)
{
    if (rule_id < 0) {
        click_chatter("[NIC %s] Unable to delete mapping for invalid controller rule ID %ld", get_device_address().c_str(), rule_id);
        return false;
    }

    if (_internal_rule_map.remove(rule_id)) {
        if (_verbose) {
            click_chatter("[NIC %s] Successfully removed mapping for rule ID %ld", get_device_address().c_str(), rule_id);
        }

        return true;
    }

    return false;
}

/**
 * Return a mapping between a controller and a NIC rule ID.
 */
uint32_t
NIC::get_internal_rule_id(const long &rule_id)
{
    if (rule_id < 0) {
        click_chatter("[NIC %s] Unable to store mapping for invalid controller rule ID %ld", get_device_address().c_str(), rule_id);
        return ERROR;
    }

    return _internal_rule_map[rule_id];
}
#endif

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk Json)

EXPORT_ELEMENT(Metron)
ELEMENT_MT_SAFE(Metron)
