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
sc_type_enum_to_str(ScType s)
{
    return String(SC_TYPES_STR_ARRAY[static_cast<uint8_t>(s)]).lower();
}

/**
 * Converts a string-based service chain type into enum.
 */
ScType
sc_type_str_to_enum(const String sc_type)
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
rx_filter_type_enum_to_str(RxFilterType rf)
{
    return String(RX_FILTER_TYPES_STR_ARRAY[static_cast<uint8_t>(rf)]).lower();
}

/**
 * Converts a string-based Rx filter type into enum.
 */
RxFilterType
rx_filter_type_str_to_enum(const String rf_str)
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

static String
parseVendorInfo(String hw_info, String key)
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
    _timer(this), _core_id(0), _timing_stats(true),
    _discovered(false), _rx_mode(FLOW)
{

}

Metron::~Metron()
{

}

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
        .read_mp ("ID",                _id)
        .read_all("NIC",               nics)
        .read_all("SLAVE_DPDK_ARGS",   _dpdk_args)
        .read_all("SLAVE_ARGS",        _args)
        .read    ("RX_MODE",           rx_mode)
        .read    ("TIMING_STATS",      _timing_stats)
        .read    ("AGENT_IP",          _agent_ip)
        .read    ("AGENT_PORT",        _agent_port)
        .read    ("DISCOVER_IP",       _discover_ip)
        .read    ("DISCOVER_PORT",     _discover_rest_port)
        .read    ("DISCOVER_PATH",     _discover_path)
        .read    ("DISCOVER_USER",     _discover_user)
        .read    ("DISCOVER_PASSWORD", _discover_password)
        .read    ("PIN_TO_CORE",       _core_id)
        .complete() < 0)
        return -1;

    unsigned max_core_nb = click_max_cpu_ids();
    if ((_core_id < 0) || (_core_id >= max_core_nb)) {
        return errh->error(
            "Cannot pin Metron agent to CPU core: %d. "
            "Use a CPU core index in [0,%d].",
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

#ifndef HAVE_CURL
    if (_discover_ip) {
        return errh->error(
            "Metron data plane agent requires controller discovery, "
            "but Click was compiled without libcurl support!"
        );
    }
#endif

#if HAVE_CURL
    // No discovery if missing information
    if (_discover_ip &&
        (!_agent_ip) || (!_discover_user) || (!_discover_password)) {
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

    // Setup pointers with the underlying NICs
    for (Element *e : nics) {
        NIC nic;
        nic.element = e;
        _nics.insert(nic.get_id(), nic);
    }

    // Confirm the mode of the underlying NICs
    return confirm_nic_mode(errh);
}

int
Metron::confirm_nic_mode(ErrorHandler *errh)
{
    auto nic = _nics.begin();
    while (nic != _nics.end()) {
        // Cast input element
        FromDPDKDevice *fd = dynamic_cast<FromDPDKDevice *>(nic.value().element);
        // Get its Rx mode
        String fd_mode = fd->get_device()->get_mode_str();

    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        if ((_rx_mode == FLOW) && (fd_mode != FlowDirector::FLOW_DIR_MODE)) {
            return errh->error(
                "Metron RX_MODE %s requires FromDPDKDevice(%s) MODE %s",
                rx_filter_type_enum_to_str(_rx_mode).c_str(),
                nic.value().get_id().c_str(),
                FlowDirector::FLOW_DIR_MODE.c_str()
            );
        }
    #endif

        // TODO: What if _rx_mode = VLAN and fd_mode = vmdq?
        //       The agent should be able to provide MAC or VLAN tags.
        if ((_rx_mode == MAC) && (fd_mode != "vmdq")) {
            return errh->error(
                "Metron RX_MODE %s requires FromDPDKDevice(%s) MODE vmdq",
                rx_filter_type_enum_to_str(_rx_mode).c_str(),
                nic.value().get_id().c_str()
            );
        }

        if ((_rx_mode == RSS) && (fd_mode != "rss")) {
            return errh->error(
                "RX_MODE %s is the default FastClick mode and requires FromDPDKDevice(%s) MODE rss",
                rx_filter_type_enum_to_str(_rx_mode).c_str(),
                nic.value().get_id().c_str()
            );
        }

        nic++;
    }

    return 0;
}

int
Metron::initialize(ErrorHandler *errh)
{
    _cpu_map.resize(get_cpus_nb(), 0);

    String hw_info = file_string("/proc/cpuinfo");
    _cpu_vendor = parseVendorInfo(hw_info, "vendor_id");
    _hw = parseVendorInfo(hw_info, "model name");
    _sw = CLICK_VERSION;

    String sw_info = shell_command_output_string("dmidecode -t 1", "", errh);
    _serial = parseVendorInfo(sw_info, "Serial Number");

#if HAVE_CURL
    // Only if user has requested discovery
    if (_discover_ip) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        for (unsigned short i=0 ; i<DISCOVERY_ATTEMPTS ; i++) {
            if ((_discovered = discover())) {
                break;
            }
        }
    }

    if (!_discovered) {
        errh->warning("To proceed, Metron controller must initiate the discovery");
    }
#endif

    _timer.initialize(this);
    _timer.move_thread(_core_id);
    _timer.schedule_after_sec(1);

    return 0;
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

        /* Compose the URL */
        String url = "http://" + _discover_ip + ":" +
                    String(_discover_rest_port) + _discover_path;

        /* Now specify the POST data */
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
            set_hw_info(rest);
            device.set("rest", rest);

            Json basic = Json::make_object();
            basic.set("driver", DEF_DISCOVER_DRIVER);
            device.set("basic", basic);
        }
        Json devices = Json::make_object();
        devices.set("rest:" + _agent_ip + ":" + String(_agent_port), device);
        j.set("devices", devices);
        String s = j.unparse(true);

        /* Curl settings */
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

        /* Send the request and get the return code in res */
        res = curl_easy_perform(curl);

        /* Check for errors */
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

        /* Always cleanup */
        curl_easy_cleanup(curl);

        return (res == CURLE_OK);
    }
#endif

    return false;
}

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
       float max_cpuload = 0;
       float total_cpuload = 0;

       for (int j = 0; j < sc->get_max_cpu_nb(); j++) {
           float cpuload = 0;
           for (int i = 0; i < sc->get_nics_nb(); i++) {
               NIC *nic = sc->get_nic_by_index(i);
               assert(nic);
               int stat_idx = (j * sc->get_nics_nb()) + i;

               String name = sc->generate_config_slave_fd_name(i, j);
               long long useless = atoll(sc->simple_call_read(name + ".useless").c_str());
               long long useful = atoll(sc->simple_call_read(name + ".useful").c_str());
               long long count = atoll(sc->simple_call_read(name + ".count").c_str());
               long long useless_diff = useless - sc->nic_stats[stat_idx].useless;
               long long useful_diff = useful - sc->nic_stats[stat_idx].useful;
               long long count_diff = count - sc->nic_stats[stat_idx].count;
               sc->nic_stats[stat_idx].useless = useless;
               sc->nic_stats[stat_idx].useful = useful;
               sc->nic_stats[stat_idx].count = count;
               if (useful_diff + useless_diff == 0) {
                   sc->nic_stats[stat_idx].load = 0;
                   // click_chatter(
                   //      "[SC %d] Load NIC %d CPU %d - %f : No data yet",
                   //      sn, i, j, sc->nic_stats[stat_idx].load
                   //  );
                   continue;
               }
               double load = (double)useful_diff /
                            (double)(useful_diff + useless_diff);
               double alpha;
               if (load > sc->nic_stats[stat_idx].load) {
                   alpha = alpha_up;
               } else {
                   alpha = alpha_down;
               }
               sc->nic_stats[stat_idx].load =
                        (sc->nic_stats[stat_idx].load * (1-alpha)) +
                        ((alpha) * load);

               // click_chatter(
               //      "[SC %d] Load NIC %d CPU %d - %f %f - diff usefull %lld useless %lld",
               //      sn, i, j, load, sc->nic_stats[stat_idx].load, useful_diff, useless_diff
               //  );

               if (sc->nic_stats[stat_idx].load > cpuload)
                   cpuload = sc->nic_stats[stat_idx].load;
           }
           sc->_cpuload[j] = cpuload;
           total_cpuload += cpuload;
           if (cpuload > max_cpuload) {
               max_cpuload = cpuload;
           }
       }

       if (sc->_autoscale) {
           sc->_total_cpuload = sc->_total_cpuload *
                        (1 - total_alpha) + max_cpuload * (total_alpha);
           if (sc->_total_cpuload > CPU_OVERLOAD_LIMIT) {
               sc->do_autoscale(1);
           } else if (sc->_total_cpuload < CPU_UNERLOAD_LIMIT) {
               sc->do_autoscale(-1);
           }
       } else {
           sc->_total_cpuload = sc->_total_cpuload * (1 - total_alpha) +
                        (total_cpuload / sc->get_used_cpu_nb()) * (total_alpha);
       }
       sn++;
       sci++;

    }
    _timer.reschedule_after_sec(1);
}

void
Metron::cleanup(CleanupStage)
{
#if HAVE_CURL
    curl_global_cleanup();
#endif
    // Delete service chains
    auto begin = _scs.begin();
    while (begin != _scs.end()) {
        // TODO: Fix
        delete begin.value();
        begin++;
    }
}

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

bool
Metron::assign_cpus(ServiceChain *sc, Vector<int> &map)
{
    int j = 0;
    if (this->get_assigned_cpus_nb() + sc->get_max_cpu_nb() >= this->get_cpus_nb()) {
        return false;
    }

    for (int i = 0; i < get_cpus_nb(); i++) {
        if (_cpu_map[i] == 0) {
            _cpu_map[i] = sc;
            map[j++] = i;
            if (j == sc->get_max_cpu_nb())
                return true;
        }
    }

    return false;
}

void
Metron::unassign_cpus(ServiceChain *sc)
{
    int j = 0;
    for (int i = 0; i < get_cpus_nb(); i++) {
        if (_cpu_map[i] == sc) {
            _cpu_map[i] = 0;
        }
    }
}

int
ServiceChain::RxFilter::apply(NIC *nic, ErrorHandler *errh)
{
    // Get the NIC index requested by the controller
    int inic = _sc->get_nic_index(nic);
    assert(inic >= 0);
    // Allocate the right number of tags
    if (values.size() <= _sc->get_nics_nb()) {
        values.resize(_sc->get_nics_nb());
    }
    values[inic].resize(_sc->get_max_cpu_nb());

    // Only MAC address is currently supported. Only thing to do is to get addr
    if (method == MAC) {
        click_chatter("Rx filters in MAC-based VMDq mode");

        Json jaddrs = Json::parse(nic->call_read("vf_mac_addr"));
        // click_chatter("VF MAC addresses: %s", nic->call_read("vf_mac_addr").c_str());

        for (int i = 0; i < _sc->get_max_cpu_nb(); i++) {
            int available_pools = atoi(nic->call_read("nb_vf_pools").c_str());
            if (available_pools <= _sc->get_cpu_map(i)) {
                return errh->error("Not enough VF pools: %d are available", available_pools);
            }
            values[inic][i] = jaddrs[_sc->get_cpu_map(i)].to_s();
        }
    } else if (method == FLOW) {
        click_chatter("Rx filters in Flow Director mode");

        int inic = _sc->get_nic_index(nic);
        assert(inic >= 0);
        if (values.size() <= _sc->get_nics_nb()) {
            values.resize(_sc->get_nics_nb());
        }
        values[inic].resize(_sc->get_max_cpu_nb());

        // TODO
        // Do we need anyhitng else here?
    } else if (method == VLAN) {
        click_chatter("Rx filters in VLAN-based VMDq mode");
        return errh->error("VLAN-based dispatching with VMDq is not implemented yet");
    } else if (method == RSS) {
        click_chatter("Rx filters in RSS mode");
        // TODO: This should be trivial to support
        return errh->error("RSS-based dispatching is not implemented yet");
    }

    return 0;
}

/**
 * Returns a service chain instance by looking up its ID
 * or NULL if no such service chain ID exists.
 */
ServiceChain *
Metron::find_service_chain_by_id(String id)
{
    return _scs.find(id);
}

/**
 * Assign CPUs to a service chain and run it.
 * If successful, the chain is added to the internal chains list.
 * Upon failure, CPUs are unassigned.
 * It is the responsibility of the caller to delete the chain upon an error.
 */
int
Metron::instantiate_service_chain(ServiceChain *sc, ErrorHandler *errh)
{
    if (!assign_cpus(sc, sc->get_cpu_map_ref())) {
        errh->error("Could not assign enough CPUs");
        return -1;
    }

    int ret = run_chain(sc, errh);
    if (ret == 0) {
        sc->status = ServiceChain::SC_OK;
        _scs.insert(sc->get_id(), sc);
        return 0;
    }

    unassign_cpus(sc);
    return -1;
}

/**
 * Run a service chain and keep a control socket to it.
 * CPUs must already be assigned.
 */
int
Metron::run_chain(ServiceChain *sc, ErrorHandler *errh)
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
            argv_char[i] = strdup(argv[i].c_str());
        }
        argv_char[argv.size()] = 0;
        if ((ret = execv(argv_char[0], argv_char))) {
            click_chatter("Could not launch slave process: %d %d", ret, errno);
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
        String conf = sc->generate_config();
        click_chatter("Writing configuration %s", conf.c_str());

        int pos = 0;
        while (pos != conf.length()) {
            ssize_t r = write(
                config_pipe[1], conf.begin() + pos, conf.length() - pos
            );
            if (r == 0 || (r == -1 && errno != EAGAIN && errno != EINTR)) {
                if (r == -1) {
                    errh->message(
                        "%s while writing configuration", strerror(errno)
                    );
                }
                break;
            } else if (r != -1) {
                pos += r;
            }
        }

        if (pos != conf.length()) {
            close(config_pipe[1]);
            close(ctl_socket[0]);
            return -1;
        } else {
            close(config_pipe[1]);
            sc->control_init(ctl_socket[0], pid);
        }

        String s;
        int v = sc->control_read_line(s);
        if (v <= 0) {
            return errh->error("Could not read from control socket: error %d", v);
        }
        if (!s.starts_with("Click::ControlSocket/1.")) {
            kill(pid, SIGKILL);
            return errh->error("Unexpected ControlSocket command");
        }
        return 0;
    }

    assert(0);
    return -1;
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

    sc->control_send_command("WRITE stop");
    _scs.remove(sc->get_id());
    unassign_cpus(sc);

    return 0;
}

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
        case h_put_chains: {
            String id = data.substring(0, data.find_left('\n'));
            String changes = data.substring(id.length() + 1);
            ServiceChain *sc = m->find_service_chain_by_id(id);
            if (!sc) {
                return errh->error(
                    "Cannot reconfigure service chain of unknown service chain ID %s",
                    id.c_str()
                );
            }
            return sc->reconfigure_from_json(Json::parse(changes), m, errh);
        }
        case h_delete_chains: {
            ServiceChain *sc = m->find_service_chain_by_id(data);
            if (!sc) {
                return errh->error(
                    "Cannot delete service chain of unknown service chain ID %s",
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
        case h_delete_rules: {
            ServiceChain *sc = m->find_service_chain_by_id(data);
            if (!sc) {
                return errh->error(
                    "Cannot delete rules of unknown service chain ID %s",
                    data.c_str()
                );
            }

            click_chatter(
                "Metron controller requested rule deletion for service chain %s",
                sc->get_id().c_str()
            );

            // TODO
            // Find the NIC and call nic->call_rx_write("flush_rules", "");

            return 0;
        }
        default: {
            errh->error("Unknown write handler: %d", what);
        }
    }

    return -1;
}

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
                    jroot.set("servicechains", jscs);
                } else {
                    ServiceChain *sc = m->find_service_chain_by_id(param);
                    if (!sc) {
                        return errh->error("Unknown service chain ID: %s", param.c_str());
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
                        jscs.push_back(begin.value()->stats_to_json());
                        begin++;
                    }
                    jroot.set("servicechains", jscs);
                } else {
                    ServiceChain *sc = m->find_service_chain_by_id(param);
                    if (!sc) {
                        return errh->error("Unknown service chain ID: %s", param.c_str());
                    }
                    jroot = sc->stats_to_json();
                }
                break;
            }
            case h_chains_rules: {
                if (param == "") {
                    click_chatter("Metron controller requested local rules for all service chains");

                    Json jscs = Json::make_array();
                    auto begin = m->_scs.begin();
                    while (begin != m->_scs.end()) {
                        jscs.push_back(begin.value()->rules_to_json());
                        begin++;
                    }
                    // jscs.push_back(ServiceChain::rules_to_json());
                    jroot.set("rules", jscs);
                } else {
                    ServiceChain *sc = m->find_service_chain_by_id(param);
                    if (!sc) {
                        return errh->error("Unknown service chain ID: %s", param.c_str());
                    }
                    click_chatter(
                        "Metron controller requested local rules for service chain %s",
                        sc->get_id().c_str()
                    );
                    jroot = sc->rules_to_json();
                }
                break;
            }
            case h_chains_proxy: {
                int pos = param.find_left("/");
                if (pos <= 0) {
                    param = "You must give a service chain ID, then a command";
                    return 0;
                }
                String ids = param.substring(0, pos);
                ServiceChain *sc = m->find_service_chain_by_id(ids);
                if (!sc) {
                    return errh->error("Unknown service chain ID: %s", ids.c_str());
                }
                param = sc->simple_call_read(param.substring(pos + 1));
                return 0;
            }
            default: {
                return errh->error("Invalid read operation in param handler");
            }
        }

        param = jroot.unparse(true);

        return 0;
    // Controller --> Metron agent
    } else if (operation == Handler::f_write) {
        intptr_t what = reinterpret_cast<intptr_t>(h->write_user_data());
        switch (what) {
            case h_chains: {
                Json jroot = Json::parse(param);
                Json jlist = jroot.get("servicechains");
                for (auto jsc : jlist) {
                    struct ServiceChain::timing_stats ts;
                    if (m->_timing_stats) {
                        ts.start = Timestamp::now_steady();
                    }

                    // Parse
                    ServiceChain *sc = ServiceChain::from_json(jsc.second, m, errh);
                    if (m->_timing_stats) {
                        ts.parse = Timestamp::now_steady();
                    }
                    if (!sc) {
                        return errh->error("Could not instantiate a service chain");
                    }

                    String sc_id = sc->get_id();
                    if (m->find_service_chain_by_id(sc_id) != 0) {
                        delete sc;
                        return errh->error(
                            "A service chain with ID %s already exists. "
                            "Delete it first.", sc_id.c_str());
                    }

                    // Instantiate
                    int ret = m->instantiate_service_chain(sc, errh);
                    if (ret != 0) {
                        delete sc;
                        return errh->error(
                            "Could not start the service chain "
                            "with ID %s", sc_id.c_str()
                        );
                    }
                    if (m->_timing_stats) {
                        ts.launch = Timestamp::now_steady();
                        sc->set_timing_stats(ts);
                    }
                }

                return 0;
            }
            case h_chains_rules: {
                click_chatter("Metron controller requested rule installation");
                // click_chatter("%s", param.c_str());

                Json jroot = Json::parse(param);
                Json jlist = jroot.get("rules");
                for (auto jsc : jlist) {
                    String sc_id = jsc.second.get_s("id");
                    click_chatter("Service chain ID: %s", sc_id.c_str());

                    ServiceChain *sc = m->find_service_chain_by_id(sc_id);

                    // Parse
                    int ret =  ServiceChain::rules_from_json(jsc.second, m, errh);
                    if (ret != 0) {
                        return errh->error("Cannot install rules: Parsing error");
                    }
                }

                return 0;
            }
            default: {
                return errh->error("Invalid write operation in param handler");
            }

        }
        return -1;
    } else {
        return errh->error("Unknown operation in param handler");
    }
}

void
Metron::add_handlers()
{
    // HTTP get handlers
    add_read_handler ("discovered",         read_handler,  h_discovered);
    add_read_handler ("resources",          read_handler,  h_resources);
    add_read_handler ("controllers",        read_handler,  h_controllers);
    add_read_handler ("stats",              read_handler,  h_stats);

    // HTTP post handlers
    add_write_handler("controllers",        write_handler, h_controllers);
    add_write_handler("put_chains",         write_handler, h_put_chains);

    // Get and POST HTTP handlers with parameters
    set_handler(
        "chains",
        Handler::f_write | Handler::f_read | Handler::f_read_param,
        param_handler, h_chains, h_chains
    );
    set_handler(
        "chains_stats", Handler::f_read | Handler::f_read_param,
        param_handler, h_chains_stats
    );
    set_handler(
        "rules", Handler::f_write | Handler::f_read | Handler::f_read_param,
        param_handler, h_chains_rules, h_chains_rules
    );
    set_handler(
        "chains_proxy", Handler::f_read | Handler::f_read_param,
        param_handler, h_chains_proxy
    );

    // HTTP delete handlers
    add_write_handler("delete_chains",      write_handler, h_delete_chains);
    add_write_handler("delete_rules",       write_handler, h_delete_rules);
    add_write_handler("delete_controllers", write_handler, h_delete_controllers);
}

void
Metron::set_hw_info(Json &j)
{
    j.set("manufacturer", Json(_cpu_vendor));
    j.set("hwVersion", Json(_hw));
    j.set("swVersion", Json("Click " + _sw));
}

Json
Metron::to_json()
{
    Json jroot = Json::make_object();

    // No controller
    if (!_discovered) {
        click_chatter(
            "Cannot publish local resources: Metron agent is not associated with a controller"
        );
        return jroot;
    }

    jroot.set("id", Json(_id));
    jroot.set("serial", Json(_serial));

    // Info
    set_hw_info(jroot);

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

Json
Metron::stats_to_json()
{
    Json jroot = Json::make_object();

    // No controller
    if (!_discovered) {
        click_chatter(
            "Cannot report statistics: Metron agent is not associated with a controller"
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
            int cpu_id = sc->get_cpu_map(j);
            float cpuload = sc->_cpuload[j];

            /* Replace the initialized values above
             * with the real monitoring data.
             */
            Json jcpu = Json::make_object();
            jcpu.set("id",   cpu_id);
            jcpu.set("load", cpuload);
            jcpu.set("busy", true);      // This CPU core is busy

            jcpus.push_back(jcpu);

            assigned_cpus++;
            busy_cpus.push_back(cpu_id);
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
        jcpu.set("busy", false);  // This CPU core is free
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

int
Metron::controllers_from_json(Json j)
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
            return -1;
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

    return 0;
}

int
Metron::delete_controller_from_json(const String &ip)
{
    // This agent is not associated with a Metron controller at the moment
    if (_discover_ip.empty()) {
        click_chatter("No controller associated with this Metron agent");
        return -1;
    }

    // Request to remove a controller must have correct IP
    if ((!ip.empty()) && (ip != _discover_ip)) {
        click_chatter("Metron agent is not associated with a Metron controller on %s", ip.c_str());
        return -1;
    }

    click_chatter(
        "Metron controller instance on %s:%d has been removed",
        _discover_ip.c_str(), _discover_port
    );

    // Reset controller information
    _discovered = false;
    _discover_ip   = "";
    _discover_port = -1;

    return 0;
}

/***************************************
 * RxFilter
 **************************************/
ServiceChain::RxFilter *
ServiceChain::RxFilter::from_json(
        Json j, ServiceChain *sc, ErrorHandler *errh)
{
    ServiceChain::RxFilter *rf = new RxFilter(sc);

    RxFilterType rf_type = rx_filter_type_str_to_enum(j.get_s("method").upper());
    if (rf_type == NONE) {
        errh->error(
            "Unsupported Rx filter mode for service chain: %s\n"
            "Supported Rx filter modes are: %s", sc->id.c_str(),
            supported_types(RX_FILTER_TYPES_STR_ARRAY).c_str()
        );
        return 0;
    }
    rf->method = rf_type;

    rf->values.resize(sc->get_nics_nb(), Vector<String>());
    Json jnic_values = j.get("values");

    int inic = 0;
    for (auto jnic : jnic_values) {
        NIC *nic = sc->get_nic_by_id(jnic.first);
        rf->values[inic].resize(jnic.second.size());
        int j = 0;
        for (auto jchild : jnic.second) {
            rf->values[inic][j++] = jchild.second.to_s();
        }
        inic++;
    }

    return rf;
}

Json
ServiceChain::RxFilter::to_json()
{
    Json j;

    j.set("method", rx_filter_type_enum_to_str(method));

    Json jnic_values = Json::make_object();
    for (int i = 0; i < _sc->get_nics_nb(); i++) {
        NIC *nic = _sc->get_nic_by_index(i);
        Json jaddrs = Json::make_array();
        for (int j = 0; j < _sc->get_max_cpu_nb(); j++) {
            jaddrs.push_back(values[i][j]);
        }
        jnic_values[nic->get_id()] = jaddrs;
    }
    j.set("values", jnic_values);

    return j;
}

/************************
 * Service Chain
 ************************/
ServiceChain::ServiceChain(Metron *m)
    : rx_filter(0), _metron(m), _total_cpuload(0)
{

}

ServiceChain::~ServiceChain()
{
    // Do not delete NICs, we are not the owner of those pointers
    if (rx_filter) {
        rx_filter->values.clear();
        delete rx_filter;
    }
}

ServiceChain *
ServiceChain::from_json(
        Json j, Metron *m, ErrorHandler *errh)
{
    ServiceChain *sc = new ServiceChain(m);
    sc->id = j.get_s("id");
    if (sc->id == "") {
        sc->id = String(m->get_service_chains_nb());
    }
    String sc_type_str = j.get_s("configType");
    ScType sc_type = sc_type_str_to_enum(sc_type_str.upper());
    if (sc_type == UNKNOWN) {
        errh->error(
            "Unsupported configuration type for service chain: %s\n"
            "Supported types are: %s",
            sc->id.c_str(), supported_types(SC_TYPES_STR_ARRAY).c_str()
        );
        return 0;
    }
    sc->config_type = sc_type;
    sc->config = j.get_s("config");
    sc->_used_cpus_nb = j.get_i("cpus");
    sc->_max_cpus_nb = j.get_i("maxCpus");
    if (sc->_used_cpus_nb > sc->_max_cpus_nb) {
        errh->error(
            "Max number of CPUs must be greater than the number of used CPUs"
        );
        return 0;
    }
    sc->_autoscale = false;
    if (!j.get("autoscale", sc->_autoscale)) {
        errh->warning("Autoscale is not present or not boolean");
    }
    sc->_cpus.resize(sc->_max_cpus_nb);
    sc->_cpuload.resize(sc->_max_cpus_nb, 0);
    Json jnics = j.get("nics");
    for (auto jnic : jnics) {
        NIC *nic = m->_nics.findp(jnic.second.as_s());
        if (!nic) {
            errh->error("Unknown NIC: %s", jnic.second.as_s().c_str());
            delete sc;
            return 0;
        }
        sc->_nics.push_back(nic);
    }
    sc->nic_stats.resize(sc->_nics.size() * sc->_max_cpus_nb,Stat());
    sc->rx_filter = ServiceChain::RxFilter::from_json(
        j.get("rxFilter"), sc, errh
    );

    return sc;
}

Json
ServiceChain::to_json()
{
    Json jsc = Json::make_object();

    jsc.set("id", get_id());
    jsc.set("rxFilter", rx_filter->to_json());
    jsc.set("configType", sc_type_enum_to_str(config_type));
    jsc.set("config", config);
    jsc.set("expandedConfig", generate_config());
    Json jcpus = Json::make_array();
    for (int i = 0; i < get_used_cpu_nb(); i++) {
        jcpus.push_back(get_cpu_map(i));
    }
    jsc.set("cpus", jcpus);
    Json jmaxcpus = Json::make_array();
    for (int i = 0; i < get_max_cpu_nb(); i++) {
        jmaxcpus.push_back(get_cpu_map(i));
    }
    jsc.set("maxCpus", jmaxcpus);
    jsc.set("autoscale", _autoscale);
    jsc.set("status", status);
    Json jnics = Json::make_array();
    for (auto n : _nics) {
        jnics.push_back(Json::make_string(n->get_id()));
    }
    jsc.set("nics", jnics);
    return jsc;
}

Json
ServiceChain::stats_to_json()
{
    Json jsc = Json::make_object();
    jsc.set("id", get_id());

    Json jcpus = Json::make_array();
    for (int j = 0; j < get_max_cpu_nb(); j ++) {
        String js = String(j);
        int avg_max = 0;
        for (int i = 0; i < get_nics_nb(); i++) {
            String is = String(i);
            int avg = atoi(
                simple_call_read("batchAvg" + is + "C" + js + ".average").c_str()
            );
            if (avg > avg_max)
                avg_max = avg;
        }
        Json jcpu = Json::make_object();
        jcpu.set("id", get_cpu_map(j));
        jcpu.set("load", _cpuload[j]);
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

        for (int j = 0; j < get_max_cpu_nb(); j ++) {
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
        jnic.set("id", get_nic_by_index(i)->get_id());
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

    if (_metron->_timing_stats) {
        jsc.set("timing_stats", _timing_stats.to_json());
        jsc.set("autoscale_timing_stats", _as_timing_stats.to_json());
    }

    return jsc;
}

Json
ServiceChain::rules_to_json()
{
    Json jsc = Json::make_object();

    // Service chain ID
    jsc.set("id", get_id());
    // jsc.set("id", "sc:id:00001");

    // Service chain's Rx filter method
    Json j;
    j.set("method", rx_filter_type_enum_to_str(rx_filter->method));
    // j.set("method", "flow");
    jsc.set("rxFilter", j);

    Json jrules = Json::make_object();

    ///////////////////////////////////////////////////
    // This is test code to be removed
    // for (int i = 0; i < 1; i++) {
    //     String is = String(i);

    //     Json jcpus = Json::make_object();
    //     for (int j = 0; j < 1; j ++) {
    //         String js = String(j);

    //         // For now it is an empty array, no rules
    //         Json jcpu_rules = Json::make_array();
    //         Json jcpu = Json::make_object();
    //         jcpu.set("ruleId", 1234567);
    //         jcpu.set("ruleContent", "ingress pattern eth type is 2048 / ipv4 proto spec 6 proto mask 0x0 src is 10.0.0.0/24 dst is 20.0.0.0/24 / end actions queue index 0 / end");
    //         jcpu_rules.push_back(jcpu);

    //         jcpus.set(js, jcpu_rules);
    //     }
    //     jrules.set("id" + is, jcpus);
    // }
    ///////////////////////////////////////////////////

    // All NICs
    for (int i = 0; i < get_nics_nb(); i++) {
        String nic_id = String(i);
        NIC *nic = _nics[i];

        Json jcpus = Json::make_object();
        // All CPU cores
        for (int j = 0; j < get_max_cpu_nb(); j ++) {
            String core_id = String(j);

            Json jcpu_rules = Json::make_array();

            // Fetch the rules for this NIC and this CPU core
            HashMap<long, String> *rules_map = nic->find_rules_by_core_id(atoi(core_id.c_str()));
            auto begin = rules_map->begin();
            while (begin != rules_map->end()) {
                long rule_id = begin.key();
                String rule = begin.value();

                Json jcpu = Json::make_object();
                jcpu.set("ruleId", rule_id);
                jcpu.set("ruleContent", rule);
                jcpu_rules.push_back(jcpu);

                begin++;
            }
            jcpus.set(core_id, jcpu_rules);
        }

        // One NIC has multiple CPU cores
        // TODO: Why this should be idX? and not fdX?
        jrules.set("id" + nic_id, jcpus);
    }

    jsc.set("nicRules", jrules);

    return jsc;
}

Json
ServiceChain::timing_stats::to_json()
{
    Json j = Json::make_object();
    j.set("parse", (parse - start).nsecval());
    j.set("launch", (launch - parse).nsecval());
    j.set("total", (launch - start).nsecval());
    return j;
}

Json
ServiceChain::autoscale_timing_stats::to_json()
{
    Json j = Json::make_object();
    j.set("autoscale", (autoscale_end - autoscale_start).nsecval());
    return j;
}

int
ServiceChain::reconfigure_from_json(Json j, Metron *m, ErrorHandler *errh)
{
    // No controller
    if (!m->_discovered) {
        click_chatter(
            "Cannot reconfigure service chain: Metron agent is not associated with a controller"
        );
        return -1;
    }

    for (auto jfield : j) {
        if (jfield.first == "cpus") {
            int new_cpus_nb = jfield.second.to_i();
            int ret;
            String response = "";
            if (new_cpus_nb == get_used_cpu_nb())
                continue;
            if (new_cpus_nb > get_used_cpu_nb()) {
                if (new_cpus_nb > get_max_cpu_nb()) {
                    return errh->error(
                        "Number of used CPUs must be less or equal "
                        "than the maximum number of CPUs!"
                    );
                }
                for (int inic = 0; inic < get_nics_nb(); inic++) {
                    for (int i = get_used_cpu_nb(); i < new_cpus_nb; i++) {
                        ret = call_write(
                            generate_config_slave_fd_name(
                                inic, get_cpu_map(i)
                            ) + ".active", response, "1"
                        );
                        if (ret < 200 || ret >= 300) {
                            return errh->error(
                                "Response to activate input was %d: %s",
                                ret, response.c_str()
                            );
                        }
                        click_chatter("Response %d: %s", ret, response.c_str());
                    }
                }
            } else {
                if (new_cpus_nb < 0) {
                    return errh->error(
                        "Number of used CPUs must be greater or equal than 0!"
                    );
                }
                for (int inic = 0; inic < get_nics_nb(); inic++) {
                    for (int i = new_cpus_nb; i < get_used_cpu_nb(); i++) {
                        int ret = call_write(
                            generate_config_slave_fd_name(
                                inic, get_cpu_map(i)
                            ) + ".active", response, "0"
                        );
                        if (ret < 200 || ret >= 300) {
                            return errh->error(
                                "Response to activate input was %d: %s",
                                ret, response.c_str()
                            );
                        }
                    }
                }
            }

            click_chatter("Number of used CPUs is now: %d", new_cpus_nb);
            _used_cpus_nb = new_cpus_nb;
            return 0;
        } else {
            return errh->error(
                "Unsupported reconfigure option: %s",
                jfield.first.c_str()
            );
        }
    }

    return 0;
}

int
ServiceChain::rules_from_json(Json j, Metron *m, ErrorHandler *errh)
{
    // No controller
    if (!m->_discovered) {
        errh->error(
            "Cannot reconfigure service chain: Metron agent is not associated with a controller"
        );
        return -1;
    }

    RxFilterType rx_filter = rx_filter_type_str_to_enum(j.get("rxFilter").get_s("method").upper());
    if (rx_filter != FLOW) {
        errh->error(
            "Cannot install rules for service chain: "
            "Invalid Rx filter mode %s is sent by the controller.",
            rx_filter_type_enum_to_str(rx_filter).c_str()
        );
        return -1;
    }
    click_chatter("       Rx Filter: %s", rx_filter_type_enum_to_str(rx_filter).c_str());

    Json jrulesAll = j.get("nicRules");
    for (auto jnic : jrulesAll) {
        String nic_id = jnic.first;
        click_chatter("             NIC: %s", nic_id.c_str());

        Json jrulesNic = jnic.second;
        for (auto jcpu : jrulesNic) {
            String cpu_id = jcpu.first;
            int cpu_index = atoi(cpu_id.c_str());
            click_chatter("             CPU: %d", cpu_index);

            Json jrulesCpu = jcpu.second;
            for (auto jrule : jrulesCpu) {
                long rule_id = jrule.second.get_i("ruleId");
                String rule = jrule.second.get_s("ruleContent");
                click_chatter("         Rule ID: %ld", rule_id);
                click_chatter("            Rule: %s", rule.c_str());

                // TODO: Store this rule when this method becomes non-static
                // NIC *nic = this->get_nic_by_id(nic_id);
                // if (!nic) {
                //     return errh->error(
                //         "Metron controller attempted to install rules on unknown NIC ID: %s",
                //         nic_id.c_str()
                //     );
                // }

                // if (!nic->add_rule(cpu_index, rule_id, rule)) {
                //     return errh->error(
                //         "Metron controller failed to store rule %ld for NIC %s and CPU core %d",
                //         rule_id, nic_id.c_str(), cpu_index
                //     );
                // }

                // if (!nic->install_rule(rule)) {
                //     return errh->error(
                //         "Metron controller failed to install rule %ld for NIC %s and CPU core %d",
                //         rule_id, nic_id.c_str(), cpu_index
                //     );
                // }
            }
        }
    }

    return 0;
}

void
ServiceChain::do_autoscale(int n_cpu_change)
{
    ErrorHandler *errh = ErrorHandler::default_handler();
    if ((Timestamp::now() - _last_autoscale).msecval() < AUTOSCALE_WINDOW) {
        return;
    }

    _last_autoscale = Timestamp::now();
    int nnew = _used_cpus_nb + n_cpu_change;
    if (nnew <= 0 || nnew > get_max_cpu_nb()) {
        return;
    }

    // Measure the time it takes to perform an autoscale
    struct ServiceChain::autoscale_timing_stats ts;
    ts.autoscale_start = Timestamp::now_steady();

    _used_cpus_nb = nnew;
    click_chatter(
        "Autoscale: Service chain %s uses %d CPU(s)",
        this->get_id().c_str(), _used_cpus_nb
    );

    String response;
    int ret = call_write("slave/rrs.max", response, String(_used_cpus_nb));
    if (ret < 200 || ret >= 300) {
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

String
ServiceChain::generate_config()
{
    String newconf = "elementclass MetronSlave {\n" + config + "\n};\n\n";
    if (_autoscale) {
        newconf += "slave :: {\n";

        newconf += "rrs :: RoundRobinSwitch(MAX " + String(get_used_cpu_nb()) + ");\n";
        newconf += "ps :: PaintSwitch();\n\n";

        for (int i = 0 ; i < get_max_cpu_nb(); i++) {
            newconf += "rrs[" + String(i) + "] -> slavep" + String(i) +
                       " :: Pipeliner(CAPACITY 8, BLOCKING false) -> "
                       "[0]ps; StaticThreadSched(slavep" +
                       String(i) + " " + String(get_cpu_map(i)) + ");\n";
        }
        newconf+="\n";

        for (int i = 0; i < get_nics_nb(); i++) {
            String is = String(i);
            newconf += "input[" + is + "] -> Paint(" + is + ") -> rrs;\n";
        }
        newconf+="\n";

        newconf += "ps => [0-" + String(get_nics_nb()-1) +
                   "]real_slave :: MetronSlave() => [0-" +
                   String(get_nics_nb()-1) + "]output }\n\n";
    } else {
        newconf += "slave :: MetronSlave();\n\n";
    }

    for (int i = 0; i < get_nics_nb(); i++) {
       String is = String(i);
       NIC *nic = get_nic_by_index(i);
       for (int j = 0; j < get_max_cpu_nb(); j++) {
           String js = String(j);
           String active = (j < get_used_cpu_nb() ? "1":"0");
           int cpuid = get_cpu_map(j);
           int queue_no = rx_filter->cpu_to_queue(nic, cpuid);
           String ename = generate_config_slave_fd_name(i, j);
           newconf += ename + " :: " + nic->element->class_name() +
                "(" + nic->get_device_id() + ", QUEUE " + String(queue_no) +
                ", N_QUEUES 1, MAXTHREADS 1, BURST 32, NUMA false, ACTIVE " +
                active + ", VERBOSE 99);\n";
           newconf += "StaticThreadSched(" + ename + " " + String(cpuid) + ");";
           newconf += ename + " " +
                // " -> batchAvg" + is + "C" + js + " :: AverageBatchCounter() " +
                " -> [" + is + "]slave;\n";
          // newconf += "Script(label s, read batchAvg" + is + "C" + js +
          //           ".average, wait 1s, goto s);\n";
       }

       // TODO: Allowed CPU bitmap
       newconf += "slaveTD" + is + " :: ToDPDKDevice(" + nic->get_device_id() + ");";
       newconf += "slave["  + is + "] -> slaveTD" + is + ";\n";

    }
    return newconf;
}

Vector<String>
ServiceChain::build_cmd_line(int socketfd)
{
    int i;
    Vector<String> argv;

    String cpulist = "";

    for (i = 0; i < click_max_cpu_ids(); i++) {
        cpulist += String(i) + (i < click_max_cpu_ids() -1? "," : "");
    }

    argv.push_back(click_path);
    argv.push_back("--dpdk");
    argv.push_back("-l");
    argv.push_back(cpulist);
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

Bitvector
ServiceChain::assigned_cpus()
{
    Bitvector b;
    b.resize(_metron->_cpu_map.size());
    for (int i = 0; i < b.size(); i ++) {
        b[i] = _metron->_cpu_map[i] == this;
    }
    return b;
}

void
ServiceChain::check_alive()
{
    if (kill(_pid, 0) != 0) {
        _metron->remove_service_chain(this, ErrorHandler::default_handler());
    } else {
        click_chatter("PID %d is alive", _pid);
    }
}

void
ServiceChain::control_init(int fd, int pid)
{
    _socket = fd;
    _pid = pid;
}

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

void
ServiceChain::control_write_line(String cmd)
{
    int n = write(_socket, (cmd + "\r\n").c_str(),cmd.length() + 1);
}

String
ServiceChain::control_send_command(String cmd)
{
    control_write_line(cmd);
    String ret;
    control_read_line(ret);

    return ret;
}

int
ServiceChain::call(
        String fnt, bool has_response, String handler,
        String &response, String params)
{
    String ret = control_send_command(
        fnt + " " + handler + (params? " " + params : "")
    );
    if (ret == "") {
        check_alive();
        return -1;
    }

    int code = atoi(ret.substring(0, 3).c_str());
    if (code >= 500) {
        response = ret.substring(4);
        return code;
    }
    if (has_response) {
        ret = ret.substring(ret.find_left("\r\n") + 2);
        assert(ret.starts_with("DATA "));
        ret = ret.substring(5);
        int eof = ret.find_left("\r\n");
        int n = atoi(ret.substring(0, eof).c_str());
        response = ret.substring(3, n);
    } else {
        response = ret.substring(4);
    }

    return code;
}

String
ServiceChain::simple_call_read(String handler)
{
    String response;

    int code = call("READ", true, handler,response, "");
    if (code >= 200 && code < 300) {
        return response;
    }

    return "";
}

int
ServiceChain::call_read(String handler, String &response, String params)
{
    return call("READ", true, handler, response, params);
}

int
ServiceChain::call_write(String handler, String &response, String params)
{
    return call("WRITE", false, handler, response, params);
}

/******************************
 * CPU
 ******************************/
int
CPU::get_id()
{
    return this->_id;
}

String
CPU::get_vendor()
{
    return this->_vendor;
}

long
CPU::get_frequency()
{
    return this->_frequency;
}

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
Json
NIC::to_json(RxFilterType rx_mode, bool stats)
{
    Json nic = Json::make_object();

    nic.set("id",get_id());
    if (!stats) {
        nic.set("vendor", call_read("vendor"));
        nic.set("driver", call_read("driver"));
        nic.set("speed", call_read("speed"));
        nic.set("status", call_read("carrier"));
        nic.set("portType", call_read("type"));
        nic.set("hwAddr", call_read("mac").replace('-',':'));
        Json jtagging = Json::make_array();
        jtagging.push_back(rx_filter_type_enum_to_str(rx_mode));
        nic.set("rxFilter", jtagging);
    } else {
        nic.set("rxCount", call_read("hw_count"));
        nic.set("rxBytes", call_read("hw_bytes"));
        nic.set("rxDropped", call_read("hw_dropped"));
        nic.set("rxErrors", call_read("hw_errors"));
        nic.set("txCount", call_tx_read("hw_count"));
        nic.set("txBytes", call_tx_read("hw_bytes"));
        nic.set("txErrors", call_tx_read("hw_errors"));
    }

    return nic;
}

String
NIC::call_read(String h)
{
    const Handler *hC = Router::handler(element, h);

    if (hC && hC->visible()) {
        return hC->call_read(element, ErrorHandler::default_handler());
    }

    return "undefined";
}

String
NIC::call_tx_read(String h)
{
    // TODO: Ensure element type
    ToDPDKDevice *td = dynamic_cast<FromDPDKDevice *>(element)->findOutputElement();
    if (!td) {
        return "Could not find matching ToDPDKDevice for NIC %s" + get_id();
    }

    const Handler *hc = Router::handler(td, h);
    if (hc && hc->visible()) {
        return hc->call_read(td, ErrorHandler::default_handler());
    }

    return "undefined";
}

int
NIC::call_rx_write(String h, const String input)
{
    FromDPDKDevice *fd = dynamic_cast<FromDPDKDevice *>(element);
    if (!fd) {
        click_chatter("Could not find matching FromDPDKDevice for NIC %s", get_id().c_str());
        return 1;
    }

    const Handler *hc = Router::handler(fd, h);
    if (hc && hc->visible()) {
        return hc->call_write(input, fd, ErrorHandler::default_handler());
    }

    click_chatter(
        "Could not find matching handler %s for NIC %s",
        h.c_str(),
        get_id().c_str()
    );

    return 1;
}

String
NIC::get_device_id()
{
    return call_read("device");
}

HashMap<long, String> *
NIC::find_rules_by_core_id(const int core_id) {
    if (core_id < 0) {
        click_chatter(
            "Unable to find rules for NIC %s: Invalid core ID %d",
            get_id().c_str(), core_id
        );
        return 0;
    }

    return _rules.find(core_id);
}

Vector<String>
NIC::rules_list_by_core_id(const int core_id) {
    Vector<String> rules;

    if (core_id < 0) {
        click_chatter(
            "Unable to find rules for NIC %s: Invalid core ID %d",
            get_id().c_str(), core_id
        );
        return rules;
    }

    HashMap<long, String> *rules_map = find_rules_by_core_id(core_id);
    if (!rules_map) {
        click_chatter(
            "No rules associated with NIC %s and CPU core %d",
            get_id().c_str(), core_id
        );
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

bool
NIC::add_rule(const int core_id, const long rule_id, const String rule)
{
    if (core_id < 0) {
        click_chatter(
            "Unable to add rule to NIC %s: Invalid core ID %d",
            get_id().c_str(), core_id
        );
        return false;
    }

    if (rule_id < 0) {
        click_chatter(
            "Unable to add rule to NIC %s: Invalid rule ID %ld",
            get_id().c_str(), rule_id
        );
        return false;
    }

    if (rule.empty()) {
        click_chatter(
            "Unable to add rule to NIC %s: Empty rule",
            get_id().c_str()
        );
        return false;
    }

    HashMap<long, String> *rules_map = find_rules_by_core_id(core_id);
    if (!rules_map) {
        rules_map = new HashMap<long, String>();
    }

    rules_map->insert(rule_id, rule);

    if (_verbose) {
        click_chatter(
            "Rule %ld added to NIC %s and mapped with CPU core %d",
            rule_id, get_id().c_str(), core_id
        );
    }

    return true;
}

bool
NIC::install_rule(const String rule)
{
    if (rule.empty()) {
        click_chatter(
            "Unable to install rule to NIC %s: Empty rule",
            get_id().c_str()
        );
        return false;
    }

    // Calls FlowDirector using FromDPDKDevice's flow handler add_rule
    if (call_rx_write("add_rule", rule) != 0) {
        click_chatter(
            "Unable to install rule '%s' to NIC %s",
            rule.c_str(), get_id().c_str()
        );

        return false;
    }

    if (_verbose) {
        click_chatter(
            "Rule '%s' installed to NIC %s",
            rule.c_str(), get_id().c_str()
        );
    }

    return true;
}

bool
NIC::remove_rule(const int core_id, const long rule_id, const String rule)
{
    if (core_id < 0) {
        click_chatter(
            "Unable to remove rule from NIC %s: Invalid core ID %d",
            get_id().c_str(), core_id
        );
        return false;
    }

    if (rule_id < 0) {
        click_chatter(
            "Unable to remove rule from NIC %s: Invalid rule ID %ld",
            get_id().c_str(), rule_id
        );
        return false;
    }

    HashMap<long, String> *rules_map = find_rules_by_core_id(core_id);
    if (!rules_map) {
        click_chatter(
            "Unable to remove rule from NIC %s: Core ID %ld has no rules",
            get_id().c_str(), rule_id
        );
        return false;
    }

    if (!rules_map->remove(rule_id)) {
        if (_verbose) {
            click_chatter(
                "Failed to remove rule %ld from NIC %s and CPU core %d",
                rule_id, get_id().c_str(), core_id
            );
        }

        return false;
    }

    if (_verbose) {
        click_chatter(
            "Rule %ld removed from NIC %s and CPU core %d",
            rule_id, get_id().c_str(), core_id
        );
    }

    return true;
}

bool NIC::remove_rules()
{
    auto begin = _rules.begin();
    while (begin != _rules.end()) {
        int core_id = begin.key();
        HashMap<long, String> *cpu_rules = begin.value();

        if (!cpu_rules) {
            continue;
        }

        cpu_rules->clear();
        delete cpu_rules;

        begin++;
    }

    _rules.clear();
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk Json)

EXPORT_ELEMENT(Metron)
ELEMENT_MT_SAFE(Metron)
