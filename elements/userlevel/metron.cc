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

#include <metron/servicechain.hh>

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    #include <click/flowdispatcher.hh>
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
    _cpu_click_to_phys.resize(click_max_cpu_ids(), 0);
#if HAVE_DPDK
    if (dpdk_enabled) {
        unsigned id = 0;
        for (unsigned i = 0; i < RTE_MAX_LCORE; i++) {
            if (rte_lcore_is_enabled(i)) {
                click_chatter("Logical CPU core %d to %d", id, i);
                _cpu_click_to_phys[id++]=i;
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
    _mirror = false;
    _slave_td_args = "";

    bool no_discovery = false;

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
        .read    ("SLAVE_TD_EXTRA",    _slave_td_args)
        .read    ("NODISCOVERY",       no_discovery)
        .read    ("MIRROR",            _mirror)
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
        nic.element = e;
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
        FromDPDKDevice *fd = dynamic_cast<FromDPDKDevice *>(nic.value().element);
        if (!fd || !fd->get_device()) {
            nic++;
            continue;
        }

        // Get its Rx mode
        String fd_mode = fd->get_device()->get_mode_str().empty() ? "unknown" : fd->get_device()->get_mode_str();

    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        // TODO: What if none of the NICs is in Metron mode?
        if ((_rx_mode == FLOW) && (fd_mode != FlowDispatcher::DISPATCHING_MODE)) {
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
        sc.get_cpu_info(i).cpu_phys_id = cpu_phys_map[i];
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

    for (unsigned i = 0; i < _cpu_map.size(); i++) {
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
    for (unsigned i = 0; i < get_cpus_nb(); i++) {
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

    for (unsigned i = 0; i < cpu_phys_map.size(); i++) {
        assert(cpu_phys_map[i] >= 0);
        sc->get_cpu_info(i).cpu_phys_id = cpu_phys_map[i];
    }
    for (unsigned i = 0; i < sc->_initial_cpus_nb; i++) {
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
            click_chatter("Metron is in fail mode... It is going to abort due to a major error !");
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

            int ret = m->delete_service_chain(sc, errh);
            if (ret == SUCCESS) {
                errh->message("Deleted service chain with ID: %s", sc->get_id().c_str());
                delete(sc);
            }

            return ret;
        }
        case h_delete_controllers: {
            return m->delete_controller_from_json((const String &) data);
        }
    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        case h_add_rules_from_file: {
            int delim = data.find_left(' ');
            // Only one argument was given
            if (delim < 0) {
                return errh->error("Handler add_rules_from_file requires 2 arguments <nic> <file-with-rules>");
            }

            // Parse and verify the first argument
            String nic_name = data.substring(0, delim).trim_space_left();

            NIC *nic = m->get_nic_by_name(nic_name);
            if (!nic) {
                return errh->error("Invalid NIC %s", nic_name.c_str());
            }
            FromDPDKDevice *fd = dynamic_cast<FromDPDKDevice *>(nic->element);
            if (!fd) {
                return errh->error("Invalid NIC %s", nic_name.c_str());
            }
            portid_t port_id = fd->get_device()->get_port_id();

            // NIC is valid, now parse the second argument
            String filename = data.substring(delim + 1).trim_space_left();

            int32_t installed_rules = FlowDispatcher::get_flow_dispatcher(port_id)->add_rules_from_file(filename);
            if (installed_rules < 0) {
                return errh->error("Failed to insert NIC flow rules from file %s", filename.c_str());
            }

            return SUCCESS;
        }
        case h_delete_rules: {
            click_chatter("Metron controller requested rule deletion");

            int32_t deleted_rules = ServiceChain::delete_rule_batch_from_json(data, m, errh);
            if (deleted_rules < 0) {
                return ERROR;
            }

            return SUCCESS;
        }
        case h_verify_nic: {
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
            FromDPDKDevice *fd = dynamic_cast<FromDPDKDevice *>(nic->element);
            if (!fd) {
                return errh->error("Invalid NIC %s", nic_name.c_str());
            }
            portid_t port_id = fd->get_device()->get_port_id();

            // NIC is valid, now parse and verify the second argument
            uint32_t rules_present = 0;
            String sec_arg = data.substring(delim + 1).trim_space_left();
            if (sec_arg.empty()) {
                // User did not specify the number of rules, infer it automatically
                rules_present = FlowDispatcher::get_flow_dispatcher(port_id)->flow_rules_count_explicit();
            } else {
                // User want to enforce the desired number of rules (assuming that he/she knows..)
                rules_present = atoi(sec_arg.c_str());
            }

            click_chatter(
                "Metron controller requested to verify the consistency of NIC %s (port %d) with %u rules present",
                nic_name.c_str(), port_id, rules_present
            );

            FlowDispatcher::get_flow_dispatcher(port_id)->rule_consistency_check(rules_present);
            return SUCCESS;
        }
        case h_flush_nics: {
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
                param = sc->_manager->command(param.substring(pos + 1));
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

                    // Parse rules from JSON
                    int32_t installed_rules = sc->rules_from_json(jsc.second, m, errh);
                    if (installed_rules < 0) {
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

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
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
    FromDPDKDevice *fd = dynamic_cast<FromDPDKDevice *>(nic->element);
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
            FlowDispatcher::get_flow_dispatcher(port_id)->min_avg_max(min, avg, max, true, true);
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
            FlowDispatcher::get_flow_dispatcher(port_id)->min_avg_max(min, avg, max, true, false);
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
            FlowDispatcher::get_flow_dispatcher(port_id)->min_avg_max(min, avg, max, false, true);
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
            FlowDispatcher::get_flow_dispatcher(port_id)->min_avg_max(min, avg, max, false, false);
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

    // HTTP post handlers
    add_write_handler("controllers",     write_handler, h_controllers);
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    add_write_handler("add_rules_from_file", write_handler, h_add_rules_from_file);
#endif

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
    set_handler(
        "chains_proxy", Handler::f_read | Handler::f_read_param,
        param_handler, h_chains_proxy
    );

    // HTTP delete handlers
    add_write_handler("delete_chains", write_handler, h_delete_chains);
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    add_write_handler("delete_rules",  write_handler, h_delete_rules);
    add_write_handler("verify_nic",    write_handler, h_verify_nic);
    add_write_handler("flush_nics",    write_handler, h_flush_nics);
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
    for (unsigned i = 0; i < get_cpus_nb(); i++) {
        uint64_t cycles_mhz = cycles_hz() / CPU::MEGA_HZ;   // In MHz
        assert(cycles_mhz > 0);

        Json jcpu = Json::make_object();
        jcpu.set("id", i);
        jcpu.set("vendor", _cpu_vendor);
        jcpu.set("frequency", cycles_mhz);
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
 * Flushes all rules from all Metron NICs.
 */
int
Metron::flush_nics()
{
    auto it = _nics.begin();
    while (it != _nics.end()) {
        NIC *nic = &it.value();

        FlowDispatcher::get_flow_dispatcher(nic->get_port_id())->flow_rules_flush();

        it++;
    }

    return SUCCESS;
}
#endif

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

        for (unsigned j = 0; j < sc->get_max_cpu_nb(); j++) {
            Json jcpu = sc->get_cpu_stats(j);
            jcpus.push_back(jcpu);

            assigned_cpus++;
            busy_cpus.push_back(sc->get_cpu_phys_id(j));
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
    _cpus.resize(max_cpu_nb,MetronCpuInfo());
    for (unsigned i = 0; i < max_cpu_nb; i++) {
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
    MetronCpuInfo &cpu = sc->get_cpu_info(j);
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

    if (m->_mirror) {
        for (unsigned i = 0; i < sc->_nics.size(); i+=2) {
            if (i + 1 < sc->_nics.size()) {
                sc->_nics[i]->mirror = sc->_nics[i + 1];
            }
        }
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
        String js = String(j);
/*        int avg_max = 0;
          for (unsigned i = 0; i < get_nics_nb(); i++) {
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

    assert(_manager);
    jsc.set("nics", _manager->nic_stats_to_json());

    jsc.set("timingStats", _timing_stats.to_json());
    jsc.set("autoScaleTimingStats", _as_timing_stats.to_json());

    return jsc;
}

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
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
        String nic_name = jnic.second.get_s("nicName");

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
            int core_id = jcpu.second.get_i("cpuId");
            assert(get_cpu_info(core_id).active());

            HashMap<uint32_t, String> rules_map;

            Json jrules = jcpu.second.get("cpuRules");
            for (auto jrule : jrules) {
                uint32_t rule_id = jrule.second.get_i("ruleId");
                String rule = jrule.second.get_s("ruleContent");
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
            click_chatter("Adding %4d rules for CPU %d with physical ID %d", rules_map.size(), core_id, phys_core_id);

            // Update a batch of rules associated with this CPU core ID
            int32_t status = nic->get_flow_dispatcher()->update_rules(rules_map, true, phys_core_id);
            if (status >= 0) {
                inserted_rules_nb += status;
            }

            if (nic->mirror) {
                click_chatter("Device %s has mirror NIC", nic_name.c_str());
                HashMap<uint32_t, String> mirror_rules_map;

                for (auto jrule : jrules) {
                    uint32_t rule_id = jrule.second.get_i("ruleId");
                    String rule = jrule.second.get_s("ruleContent");
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

                status = nic->mirror->get_flow_dispatcher()->update_rules(mirror_rules_map, true, phys_core_id);
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

        jnic.set("nicName", nic->get_name());

        Json jcpus_array = Json::make_array();

        // One NIC can dispatch to multiple CPU cores
        for (unsigned j = 0; j < get_max_cpu_nb(); j++) {
            // Fetch the rules for this NIC and this CPU core
            HashMap<uint32_t, String> *rules_map = nic->get_flow_cache()->rules_map_by_core_id(j);
            if (!rules_map || rules_map->empty()) {
                continue;
            }

            Json jcpu = Json::make_object();
            jcpu.set("cpuId", j);

            Json jrules = Json::make_array();

            auto begin = rules_map->begin();
            while (begin != rules_map->end()) {
                uint32_t rule_id = begin.key();
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

        if (!nic->get_flow_cache()->has_rules()) {
            it++;
            continue;
        }

        // Get the internal rule ID from the flow cache
        int32_t int_rule_id = nic->get_flow_cache()->internal_from_global_rule_id(rule_id);

        // This internal rule ID exists, we can proceed with the deletion
        if (int_rule_id >= 0) {
            uint32_t rule_ids[1] = {(uint32_t) int_rule_id};
            return (nic->get_flow_dispatcher()->flow_rules_delete(rule_ids, 1) == 1)? SUCCESS : ERROR;
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

        if (!nic->get_flow_cache()->has_rules()) {
            it++;
            continue;
        }

        bool nic_found = false;
        for (uint32_t i = 0; i < rules_vec.size(); i++) {
            uint32_t rule_id = atol(rules_vec[i].c_str());
            int32_t int_rule_id = nic->get_flow_cache()->internal_from_global_rule_id(rule_id);

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
    return n.get_flow_dispatcher()->flow_rules_delete(rule_ids, rules_nb);
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
        get_cpu_info(last_idx + (n_cpu_change>0?i:-1)).set_active(n_cpu_change > 0);
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

    // NICs require an additional parameter if in Flow Director mode
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
            new_conf += "slaveTD" + is + " :: Null -> " + _metron->_slave_td_args + "ToDPDKDevice(" + nic->get_device_address() + ", QUEUE " + String(queue_no) + ", VERBOSE 99);\n";
        } else {
            new_conf += "slaveTD" + is + " :: ExactCPUSwitch();\n";
            for (unsigned j = 0; j < get_max_cpu_nb(); j++) {
                String js = String(j);
                assert(get_cpu_info(j).assigned());
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
        b[i] = _cpus[i].active();
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
        int pid = _cpus[i].cpu_phys_id;
        if (pid >= 0) {
            b[pid] = true;
        }
    }
    return b;
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
 */
String
NIC::get_device_address()
{
    // TODO: Returning the PCI address would be better
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
    ToDPDKDevice *td = dynamic_cast<FromDPDKDevice *>(element)->find_output_element();
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

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk Json)

EXPORT_ELEMENT(Metron)
ELEMENT_MT_SAFE(Metron)
