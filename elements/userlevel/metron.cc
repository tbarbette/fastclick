// -*- c-basic-offset: 4; related-file-name: "metron.hh" -*-
/*
 * metron.{cc,hh} -- element that deploys, monitors, and
 * (re)configures multiple NFV service chains driven by
 * a remote controller
 *
 * Copyright (c) 2017 Tom Barbette, University of Liège
 * Copyright (c) 2017 Georgios Katsikas, RISE SICS AB
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

#if HAVE_CURL
    #include <curl/curl.h>
#endif

CLICK_DECLS

Metron::Metron() :
    _timing_stats(true), _timer(this), _discovered(false)
{

}

Metron::~Metron()
{

}

int Metron::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Vector<Element *> nics;
    _agent_port    = DEF_AGENT_PORT;
    _discover_port = DEF_DISCOVER_PORT;
    _discover_user = DEF_DISCOVER_USER;
    _discover_path = DEF_DISCOVER_PATH;

    if (Args(conf, this, errh)
        .read_mp ("ID",                _id)
        .read_all("NIC",               nics)
        .read_all("SLAVE_DPDK_ARGS",   _dpdk_args)
        .read_all("SLAVE_ARGS",        _args)
        .read    ("TIMING_STATS",      _timing_stats)
        .read    ("AGENT_IP",          _agent_ip)
        .read    ("AGENT_PORT",        _agent_port)
        .read    ("DISCOVER_IP",       _discover_ip)
        .read    ("DISCOVER_PORT",     _discover_port)
        .read    ("DISCOVER_PATH",     _discover_path)
        .read    ("DISCOVER_USER",     _discover_user)
        .read    ("DISCOVER_PASSWORD", _discover_password)
        .complete() < 0)
        return -1;

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
    if ((_agent_port    <= 0) || (_agent_port    > UINT16_MAX) ||
        (_discover_port <= 0) || (_discover_port > UINT16_MAX)) {
        return errh->error("Invalid port number");
    }
#endif

    for (Element *e : nics) {
        NIC nic;
        nic.element = e;
        _nics.insert(nic.getId(), nic);
    }

    return 0;
}

static String parseVendorInfo(String hwInfo, String key)
{
    String s;
    s = hwInfo.substring(hwInfo.find_left(key) + key.length());
    int pos = s.find_left(':') + 2;
    s = s.substring(pos, s.find_left("\n") - pos);

    return s;
}

int Metron::initialize(ErrorHandler *errh)
{
    _cpu_map.resize(getCpuNr(), 0);

    String hwInfo = file_string("/proc/cpuinfo");
    _cpu_vendor = parseVendorInfo(hwInfo, "vendor_id");
    _hw = parseVendorInfo(hwInfo, "model name");
    _sw = CLICK_VERSION;

    String swInfo = shell_command_output_string("dmidecode -t 1", "", errh);
    _serial = parseVendorInfo(swInfo, "Serial Number");

    _timer.initialize(this);
    _timer.schedule_after_sec(1);

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
    return 0;
}

bool Metron::discover()
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
                    String(_discover_port) + _discover_path;

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
            setHwInfo(rest);
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
            click_chatter(
                "Successfully advertised features to Metron controller %s:%d\n",
                _discover_ip.c_str(), _discover_port
            );
        }

        /* Always cleanup */
        curl_easy_cleanup(curl);

        return (res == CURLE_OK);
    }

    return false;
#endif
}

void Metron::run_timer(Timer *t)
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

       for (int j = 0; j < sc->getMaxCpuNr(); j++) {
           float cpuload = 0;
           for (int i = 0; i < sc->getNICNr(); i++) {
               NIC *nic = sc->getNICByIndex(i);
               assert(nic);
               int stat_idx = (j * sc->getNICNr()) + i;

               String name = sc->generateConfigSlaveFDName(i, j);
               long long useless = atoll(sc->simpleCallRead(name + ".useless").c_str());
               long long useful = atoll(sc->simpleCallRead(name + ".useful").c_str());
               long long count = atoll(sc->simpleCallRead(name + ".count").c_str());
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
               sc->doAutoscale(1);
           } else if (sc->_total_cpuload < CPU_UNERLOAD_LIMIT) {
               sc->doAutoscale(-1);
           }
       } else {
           sc->_total_cpuload = sc->_total_cpuload * (1 - total_alpha) +
                        (total_cpuload / sc->getUsedCpuNr()) * (total_alpha);
       }
       sn++;
       sci++;

    }
    _timer.reschedule_after_sec(1);
}

void Metron::cleanup(CleanupStage)
{
#if HAVE_CURL
    curl_global_cleanup();
#endif
    // Delete service chains
    auto begin = _scs.begin();
    while (begin != _scs.end()) {
        delete begin.value();
    }
}

int Metron::getAssignedCpuNr()
{
    int tot = 0;
    for (int i = 0; i < getCpuNr(); i++) {
        if (_cpu_map[i] != 0) {
            tot++;
        }
    }

    return tot;
}

bool Metron::assignCpus(ServiceChain *sc, Vector<int> &map)
{
    int j = 0;
    if (this->getAssignedCpuNr() + sc->getMaxCpuNr() >= this->getCpuNr()) {
        return false;
    }

    for (int i = 0; i < getCpuNr(); i++) {
        if (_cpu_map[i] == 0) {
            _cpu_map[i] = sc;
            map[j++] = i;
            if (j == sc->getMaxCpuNr())
                return true;
        }
    }

    return false;
}

void Metron::unassignCpus(ServiceChain *sc)
{
    int j = 0;
    for (int i = 0; i < getCpuNr(); i++) {
        if (_cpu_map[i] == sc) {
            _cpu_map[i] = 0;
        }
    }
}


int ServiceChain::RxFilter::apply(NIC *nic, ErrorHandler *errh)
{
    //Only MAC address is currently supported. Only thing to do is to get addr
    Json jaddrs = Json::parse(nic->callRead("vf_mac_addr"));
    int inic = _sc->getNICIndex(nic);
    assert(inic >= 0);
    if (values.size() <= _sc->getNICNr()) {
        values.resize(_sc->getNICNr());
    }
    values[inic].resize(_sc->getMaxCpuNr());

    for (int i = 0; i < _sc->getMaxCpuNr() ; i++) {
         if (atoi(nic->callRead("nb_vf_pools").c_str()) <= _sc->getCpuMap(i)) {
             return errh->error("Not enough VF pools");
         }
         values[inic][i] = jaddrs[_sc->getCpuMap(i)].to_s();
    }

    return 0;
}

ServiceChain *Metron::findChainById(String id)
{
    return _scs.find(id);
}

/**
 * Assign CPUs to a service chain and run it.
 * If successful, the chain is added to the internal chains list.
 * Upon failure, CPUs are unassigned.
 * It is the responsibility of the caller to delete the chain upon an error.
 */
int Metron::instantiateChain(ServiceChain *sc, ErrorHandler *errh)
{
    if (!assignCpus(sc,sc->getCpuMapRef())) {
        errh->error("Could not assign enough CPUs");
        return -1;
    }

    int ret = runChain(sc, errh);
    if (ret == 0) {
        sc->status = ServiceChain::SC_OK;
        _scs.insert(sc->getId(), sc);
        return 0;
    }

    unassignCpus(sc);
    return -1;
}

/**
 * Run a service chain and keep a control socket to it.
 * CPUs must already be assigned.
 */
int Metron::runChain(ServiceChain *sc, ErrorHandler *errh)
{
    for (int i = 0; i < sc->getNICNr(); i++) {
        if (sc->rxFilter->apply(sc->getNICByIndex(i), errh) != 0) {
            return errh->error("Could not apply RX filter");
        }
    }
    int configpipe[2], ctlsocket[2];
    if (pipe(configpipe) == -1)
        return errh->error("Could not create pipe");
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, ctlsocket) == -1)
        return errh->error("Could not create socket");

    // Launch slave
    int pid = fork();
    if (pid == -1) {
        return errh->error("Fork error. Too many processes?");
    }
    if (pid == 0) {
        int i;

        int ret;

        close(0);
        dup2(configpipe[0], 0);
        close(configpipe[0]);
        close(configpipe[1]);
        close(ctlsocket[0]);

        Vector<String> argv = sc->buildCmdLine(ctlsocket[1]);

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
        close(configpipe[0]);
        close(ctlsocket[1]);
        int flags = 1;
        /*int fd = ctlsocket[0];
        if (ioctl(fd, FIONBIO, &flags) != 0) {
            flags = fcntl(fd, F_GETFL);
            if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
                return errh->error("%s", strerror(errno));
        }
        */
        String conf = sc->generateConfig();
        click_chatter("Writing configuration %s", conf.c_str());

        int pos = 0;
        while (pos != conf.length()) {
            ssize_t r = write(
                configpipe[1], conf.begin() + pos, conf.length() - pos
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
            close(configpipe[1]);
            close(ctlsocket[0]);
            return -1;
        } else {
            close(configpipe[1]);
            sc->controlInit(ctlsocket[0], pid);
        }
        String s;
        int v = sc->controlReadLine(s);
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
int Metron::removeChain(ServiceChain *sc, ErrorHandler *)
{
    sc->controlSendCommand("WRITE stop");
    _scs.remove(sc->getId());
    unassignCpus(sc);
    return 0;
}

String Metron::read_handler(Element *e, void *user_data)
{
    Metron *m = static_cast<Metron *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    Json jroot = Json::make_object();

    switch (what) {
        case h_discovered: {
            return m->_discovered? "true":"false";
        }
        case h_resources: {
            jroot = m->toJSON();
            break;
        }
        case h_stats: {
            jroot = m->statsToJSON();
            break;
        }

    }

    return jroot.unparse(true);
}

int Metron::write_handler(
        const String &data, Element *e, void *user_data, ErrorHandler *errh)
{
    Metron *m = static_cast<Metron *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    switch (what) {
        case h_delete_chains: {
            ServiceChain *sc = m->findChainById(data);
            if (sc == 0) {
                return errh->error("Unknown ID %s", data.c_str());
            }

            int ret = m->removeChain(sc, errh);
            if (ret == 0) {
                delete(sc);
            }

            return ret;
        }
        case h_put_chains: {
            String id = data.substring(0, data.find_left('\n'));
            String changes = data.substring(id.length() + 1);
            ServiceChain *sc = m->findChainById(id);
            if (sc == 0) {
                return errh->error("Unknown ID %s", id.c_str());
            }
            return sc->reconfigureFromJSON(Json::parse(changes), m, errh);
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
    if (operation == Handler::f_read) {
        Json jroot = Json::make_object();

        intptr_t what = reinterpret_cast<intptr_t>(h->read_user_data());
        switch (what) {
            case h_chains: {
                if (param == "") {
                    Json jscs = Json::make_array();
                    auto begin = m->_scs.begin();
                    while (begin != m->_scs.end()) {
                        jscs.push_back(begin.value()->toJSON());
                        begin++;
                    }
                    jroot.set("servicechains",jscs);
                } else {
                    ServiceChain *sc = m->findChainById(param);
                    if (!sc) {
                        return errh->error("Unknown ID: %s", param.c_str());
                    }
                    jroot = sc->toJSON();
                }
                break;
            }
            case h_chains_stats: {
                if (param == "") {
                    Json jscs = Json::make_array();
                    auto begin = m->_scs.begin();
                    while (begin != m->_scs.end()) {
                        jscs.push_back(begin.value()->statsToJSON());
                        begin++;
                    }
                    jroot.set("servicechains",jscs);
                } else {
                    ServiceChain *sc = m->findChainById(param);
                    if (!sc) {
                        return errh->error("Unknown ID: %s", param.c_str());
                    }
                    jroot = sc->statsToJSON();
                }
                break;
            }
            case h_chains_proxy: {
                int pos = param.find_left("/");
                if (pos <= 0) {
                    param = "You must give an ID, then a command";
                    return 0;
                }
                String ids = param.substring(0, pos);
                ServiceChain *sc = m->findChainById(ids);
                if (!sc) {
                    return errh->error("Unknown ID: %s", ids.c_str());
                }
                param = sc->simpleCallRead(param.substring(pos + 1));
                return 0;
            }
            default:
            {
                return errh->error("Invalid operation");
            }
        }

        param = jroot.unparse(true);

        return 0;
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
                    ServiceChain *sc = ServiceChain::fromJSON(jsc.second, m, errh);
                    if (m->_timing_stats) {
                        ts.parse = Timestamp::now_steady();
                    }
                    if (!sc) {
                        return errh->error("Could not instantiate a service chain");
                    }
                    if (m->findChainById(sc->id) != 0) {
                        delete sc;
                        return errh->error(
                            "A service chain with ID %s already exists. "
                            "Delete it first.", sc->id.c_str());
                    }

                    // Instantiate
                    int ret = m->instantiateChain(sc, errh);
                    if (ret != 0) {
                        delete sc;
                        return errh->error(
                            "Could not start the service chain "
                            "with ID %s", sc->id.c_str()
                        );
                    }
                    if (m->_timing_stats) {
                        ts.launch = Timestamp::now_steady();
                        sc->setTimingStats(ts);
                    }
                }
                return 0;
            }
        }
        return -1;
    } else {
        return errh->error("Unknown operation");
    }
}

void Metron::add_handlers()
{
    add_read_handler("resources", read_handler, h_resources);
    add_read_handler("stats", read_handler, h_stats);
    add_read_handler("discovered", read_handler, h_discovered);
    add_write_handler("delete_chains", write_handler, h_delete_chains);
    add_write_handler("put_chains", write_handler, h_put_chains);

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
        "chains_proxy", Handler::f_write | Handler::f_read | Handler::f_read_param,
        param_handler, h_chains_proxy
    );
}

void Metron::setHwInfo(Json &j)
{
    j.set("manufacturer", Json(_cpu_vendor));
    j.set("hwVersion", Json(_hw));
    j.set("swVersion", Json("Click " + _sw));
}

Json Metron::toJSON()
{
    Json jroot = Json::make_object();
    jroot.set("id", Json(_id));
    jroot.set("serial", Json(_serial));

    // Info
    setHwInfo(jroot);

    // CPU resources
    Json jcpus = Json::make_array();
    for (int i = 0; i < getCpuNr(); i++) {
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
        jnics.push_back(begin.value().toJSON(false));
        begin++;
    }
    jroot.set("nics", jnics);

    return jroot;
}

Json Metron::statsToJSON()
{
    Json jroot = Json::make_object();

    // High-level CPU resources
    jroot.set("busyCpus",Json(getAssignedCpuNr()));
    jroot.set("freeCpus",Json(getCpuNr() - getAssignedCpuNr()));

    // Per core load
    Json jcpus = Json::make_array();

    /**
     * First, go through the active chains and search for
     * CPUs with some real load.
     * Mark them so that we can find the idle ones next.
     */
    int assignedCpus = 0;
    Vector<int> busyCpus;
    auto sci = _scs.begin();
    while (sci != _scs.end()) {
        ServiceChain *sc = sci.value();

        for (int j = 0; j < sc->getMaxCpuNr(); j++) {
            int cpuId = sc->getCpuMap(j);
            float cpuload = sc->_cpuload[j];

            /* Replace the initialized values above
             * with the real monitoring data.
             */
            Json jcpu = Json::make_object();
            jcpu.set("id",   cpuId);
            jcpu.set("load", cpuload);
            jcpu.set("busy", true);      // This CPU core is busy

            jcpus.push_back(jcpu);

            assignedCpus++;
            busyCpus.push_back(cpuId);
        }

        sci++;
    }

    // Now, inititialize the load of each idle core to 0
    for (int j = 0; j < getCpuNr(); j++) {
        int *found = find(busyCpus.begin(), busyCpus.end(), j);
        // This is a busy one
        if (found != busyCpus.end()) {
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
    assert(jcpus.size() == getCpuNr());
    assert(assignedCpus == getAssignedCpuNr());

    jroot.set("cpus", jcpus);

    // NIC resources
    Json jnics = Json::make_array();
    auto begin = _nics.begin();
    while (begin != _nics.end()) {
        jnics.push_back(begin.value().toJSON(true));
        begin++;
    }
    jroot.set("nics", jnics);

    return jroot;
}

/***************************************
 * RxFilter
 **************************************/
ServiceChain::RxFilter *ServiceChain::RxFilter::fromJSON(
        Json j, ServiceChain *sc, ErrorHandler *errh)
{
    ServiceChain::RxFilter *rf = new RxFilter(sc);
    rf->method = j.get_s("method").lower();
    if (rf->method != "mac") {
        errh->error(
            "Unsupported RX Filter method: %s", rf->method.c_str()
        );
        return 0;
    }
    rf->values.resize(sc->getNICNr(), Vector<String>());
    Json jnic_values = j.get("values");

    int inic = 0;
    for (auto jnic : jnic_values) {
        NIC *nic = sc->getNICById(jnic.first);
        rf->values[inic].resize(jnic.second.size());
        int j = 0;
        for (auto jchild : jnic.second) {
            rf->values[inic][j++] = jchild.second.to_s();
        }
        inic++;
    }

    return rf;
}

Json ServiceChain::RxFilter::toJSON()
{
    Json j;

    j.set("method", method);
    Json jnic_values = Json::make_object();
    for (int i = 0; i < _sc->getNICNr(); i++) {
        NIC *nic = _sc->getNICByIndex(i);
        Json jaddrs = Json::make_array();
        for (int j = 0; j < _sc->getMaxCpuNr(); j++) {
            jaddrs.push_back(values[i][j]);
        }
        jnic_values[nic->getId()] = jaddrs;
    }
    j.set("values", jnic_values);

    return j;
}

/************************
 * Service Chain
 ************************/
ServiceChain::ServiceChain(Metron *m)
    : rxFilter(0), _metron(m), _total_cpuload(0)
{

}

ServiceChain::~ServiceChain()
{
    // Do not delete NICs, we are not the owner of those pointers
    if (rxFilter) {
        delete rxFilter;
    }
}

ServiceChain *ServiceChain::fromJSON(
        Json j, Metron *m, ErrorHandler *errh)
{
    ServiceChain *sc = new ServiceChain(m);
    sc->id = j.get_s("id");
    if (sc->id == "") {
        sc->id = String(m->getNbChains());
    }
    sc->config = j.get_s("config");
    sc->_used_cpu_nr = j.get_i("cpus");
    sc->_max_cpu_nr = j.get_i("maxCpus");
    if (sc->_used_cpu_nr > sc->_max_cpu_nr) {
        errh->error(
            "Max CPU number must be greater than the used CPU number"
        );
        return 0;
    }
    sc->_autoscale = false;
    if (!j.get("autoscale", sc->_autoscale)) {
        errh->warning("Autoscale is not present or not boolean");
    }
    sc->_cpus.resize(sc->_max_cpu_nr);
    sc->_cpuload.resize(sc->_max_cpu_nr, 0);
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
    sc->nic_stats.resize(sc->_nics.size() * sc->_max_cpu_nr,Stat());
    sc->rxFilter = ServiceChain::RxFilter::fromJSON(
        j.get("rxFilter"), sc, errh
    );

    return sc;
}

Json ServiceChain::toJSON()
{
    Json jsc = Json::make_object();

    jsc.set("id", getId());
    jsc.set("rxFilter", rxFilter->toJSON());
    jsc.set("config", config);
    jsc.set("expandedConfig", generateConfig());
    Json jcpus = Json::make_array();
    for (int i = 0; i < getUsedCpuNr(); i++) {
        jcpus.push_back(getCpuMap(i));
    }
    jsc.set("cpus", jcpus);
    Json jmaxcpus = Json::make_array();
    for (int i = 0; i < getMaxCpuNr(); i++) {
        jmaxcpus.push_back(getCpuMap(i));
    }
    jsc.set("maxCpus", jmaxcpus);
    jsc.set("autoscale", _autoscale);
    jsc.set("status", status);
    Json jnics = Json::make_array();
    for (auto n : _nics) {
        jnics.push_back(Json::make_string(n->getId()));
    }
    jsc.set("nics", jnics);
    return jsc;
}

Json ServiceChain::statsToJSON()
{
    Json jsc = Json::make_object();
    jsc.set("id", getId());

    Json jcpus = Json::make_array();
    for (int j = 0; j < getMaxCpuNr(); j ++) {
        String js = String(j);
        int avg_max = 0;
        for (int i = 0; i < getNICNr(); i++) {
            String is = String(i);
            int avg = atoi(
                simpleCallRead("batchAvg" + is + "C" + js + ".average").c_str()
            );
            if (avg > avg_max)
                avg_max = avg;
        }
        Json jcpu = Json::make_object();
        jcpu.set("id", getCpuMap(j));
        jcpu.set("load", _cpuload[j]);
        jcpus.push_back(jcpu);
    }
    jsc.set("cpus", jcpus);

    Json jnics = Json::make_array();
    for (int i = 0; i < getNICNr(); i++) {
        String is = String(i);
        uint64_t rx_count   = 0;
        uint64_t rx_bytes   = 0;
        uint64_t rx_dropped = 0;
        uint64_t rx_errors  = 0;
        uint64_t tx_count   = 0;
        uint64_t tx_bytes   = 0;
        uint64_t tx_dropped = 0;
        uint64_t tx_errors  = 0;

        for (int j = 0; j < getMaxCpuNr(); j ++) {
            String js = String(j);
            rx_count += atol(
                simpleCallRead("slaveFD" + is + "C" + js + ".count").c_str()
            );
            // rx_bytes += atol(
            //     simpleCallRead( "slaveFD" + is + "C" + js + ".bytes").c_str()
            // );
            rx_dropped += atol(
                simpleCallRead("slaveFD" + is + "C" + js + ".dropped").c_str()
            );
            // rx_errors += atol(
            //     simpleCallRead( "slaveFD" + is + "C" + js + ".errors").c_str()
            // );

        }
        tx_count += atol(
            simpleCallRead("slaveTD" + is + ".count").c_str()
        );
        // tx_bytes += atol(
        //     simpleCallRead("slaveTD" + is + ".bytes").c_str()
        // );
        tx_dropped += atol(
            simpleCallRead("slaveTD" + is + ".dropped").c_str()
        );
        // tx_errors += atol(
        //     simpleCallRead("slaveTD" + is + ".errors").c_str()
        // );

        Json jnic = Json::make_object();
        jnic.set("id", getNICByIndex(i)->getId());
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
        jsc.set("timing_stats", _timing_stats.toJSON());
        jsc.set("autoscale_timing_stats", _as_timing_stats.toJSON());
    }

    return jsc;
}

Json ServiceChain::timing_stats::toJSON()
{
    Json j = Json::make_object();
    j.set("parse", (parse - start).nsecval());
    j.set("launch", (launch - parse).nsecval());
    j.set("total", (launch - start).nsecval());
    return j;
}

Json ServiceChain::autoscale_timing_stats::toJSON()
{
    Json j = Json::make_object();
    j.set("autoscale", (autoscale_end - autoscale_start).nsecval());
    return j;
}

int ServiceChain::reconfigureFromJSON(Json j, Metron *m, ErrorHandler *errh)
{
    for (auto jfield : j) {
        if (jfield.first == "cpus") {
            int newCpusNr = jfield.second.to_i();
            int ret;
            String response = "";
            if (newCpusNr == getUsedCpuNr())
                continue;
            if (newCpusNr > getUsedCpuNr()) {
                if (newCpusNr > getMaxCpuNr()) {
                    return errh->error(
                        "Number of used CPUs must be less or equal "
                        "than the max number of CPUs!"
                    );
                }
                for (int inic = 0; inic < getNICNr(); inic++) {
                    for (int i = getUsedCpuNr(); i < newCpusNr; i++) {
                        ret = callWrite(
                            generateConfigSlaveFDName(
                                inic, getCpuMap(i)
                            ) + ".active", response, "1"
                        );
                        if (ret < 200 || ret >= 300) {
                            return errh->error(
                                "Response to activate input was %d: %s",
                                ret, response.c_str()
                            );
                        }
                        click_chatter("Response %d %s", ret, response.c_str());
                    }
                }
            } else {
                if (newCpusNr < 0) {
                    return errh->error(
                        "Number of used CPUs must be greater or equal than 0!"
                    );
                }
                for (int inic = 0; inic < getNICNr(); inic++) {
                    for (int i = newCpusNr; i < getUsedCpuNr(); i++) {
                        int ret = callWrite(
                            generateConfigSlaveFDName(
                                inic, getCpuMap(i)
                            ) + ".active", response, "0"
                        );
                        if (ret < 200 || ret >= 300) {
                            return errh->error(
                                "Response to activate input was %d : %s",
                                ret, response.c_str()
                            );
                        }
                    }
                }
            }

            click_chatter("Used CPU number is now: %d", newCpusNr);
            _used_cpu_nr = newCpusNr;
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

void ServiceChain::doAutoscale(int nCpuChange)
{
    ErrorHandler *errh = ErrorHandler::default_handler();
    if ((Timestamp::now() - _last_autoscale).msecval() < 5000) {
        return;
    }

    _last_autoscale = Timestamp::now();
    int nnew = _used_cpu_nr + nCpuChange;
    if (nnew <= 0 || nnew > getMaxCpuNr()) {
        return;
    }

    // Measure the time it takes to perform an autoscale
    struct ServiceChain::autoscale_timing_stats ts;
    ts.autoscale_start = Timestamp::now_steady();

    _used_cpu_nr = nnew;
    click_chatter(
        "Autoscale: Service chain %s uses %d CPU(s)",
        this->getId().c_str(), _used_cpu_nr
    );

    String response;
    int ret = callWrite("slave/rrs.max", response, String(_used_cpu_nr));
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
    this->setAutoscaleTimingStats(ts);
}

String ServiceChain::generateConfig()
{
    String newconf = "elementclass MetronSlave {\n" + config + "\n};\n\n";
    if (_autoscale) {
        newconf += "slave :: {\n";

        newconf += "rrs :: RoundRobinSwitch(MAX " + String(getUsedCpuNr()) + ");\n";
        newconf += "ps :: PaintSwitch();\n\n";

        for (int i = 0 ; i < getMaxCpuNr(); i++) {
            newconf += "rrs[" + String(i) + "] -> slavep" + String(i) +
                       " :: Pipeliner(CAPACITY 8, BLOCKING false) -> "
                       "[0]ps; StaticThreadSched(slavep" +
                       String(i) + " " + String(getCpuMap(i)) + ");\n";
        }
        newconf+="\n";

        for (int i = 0; i < getNICNr(); i++) {
            String is = String(i);
            newconf += "input[" + is + "] -> Paint(" + is + ") -> rrs;\n";
        }
        newconf+="\n";

        newconf += "ps => [0-" + String(getNICNr()-1) +
                   "]real_slave :: MetronSlave() => [0-" +
                   String(getNICNr()-1) + "]output }\n\n";
    } else {
        newconf += "slave :: MetronSlave();\n\n";
    }

    for (int i = 0; i < getNICNr(); i++) {
       String is = String(i);
       NIC *nic = getNICByIndex(i);
       for (int j = 0; j < getMaxCpuNr(); j++) {
           String js = String(j);
           String active = (j < getUsedCpuNr() ? "1":"0");
           int cpuid = getCpuMap(j);
           int queue_no = rxFilter->cpuToQueue(nic, cpuid);
           String ename = generateConfigSlaveFDName(i, j);
           newconf += ename + " :: " + nic->element->class_name() +
                "(" + nic->getDeviceId() + ", QUEUE " + String(queue_no) +
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
       newconf += "slaveTD" + is + " :: ToDPDKDevice(" + nic->getDeviceId() + ");";
       newconf += "slave["  + is + "] -> slaveTD" + is + ";\n";

    }
    return newconf;
}

Vector<String> ServiceChain::buildCmdLine(int socketfd)
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


Bitvector ServiceChain::assignedCpus()
{
    Bitvector b;
    b.resize(_metron->_cpu_map.size());
    for (int i = 0; i < b.size(); i ++) {
        b[i] = _metron->_cpu_map[i] == this;
    }
    return b;
}

void ServiceChain::checkAlive()
{
    if (kill(_pid, 0) != 0) {
        _metron->removeChain(this, ErrorHandler::default_handler());
    } else {
        click_chatter("PID %d is alive", _pid);
    }
}

void ServiceChain::controlInit(int fd, int pid)
{
    _socket = fd;
    _pid = pid;
}

int ServiceChain::controlReadLine(String &line)
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

void ServiceChain::controlWriteLine(String cmd)
{
    int n = write(_socket, (cmd + "\r\n").c_str(),cmd.length() + 1);
}

String ServiceChain::controlSendCommand(String cmd)
{
    controlWriteLine(cmd);
    String ret;
    controlReadLine(ret);

    return ret;
}

int ServiceChain::call(
        String fnt, bool hasResponse, String handler,
        String &response, String params)
{
    String ret = controlSendCommand(
        fnt + " " + handler + (params? " " + params : "")
    );
    if (ret == "") {
        checkAlive();
        return -1;
    }

    int code = atoi(ret.substring(0, 3).c_str());
    if (code >= 500) {
        response = ret.substring(4);
        return code;
    }
    if (hasResponse) {
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

String ServiceChain::simpleCallRead(String handler)
{
    String response;

    int code = call("READ", true, handler,response, "");
    if (code >= 200 && code < 300) {
        return response;
    }

    return "";
}

int ServiceChain::callRead(String handler, String &response, String params)
{
    return call("READ", true, handler, response, params);
}

int ServiceChain::callWrite(String handler, String &response, String params)
{
    return call("WRITE", false, handler, response, params);
}

/******************************
 * CPU
 ******************************/
int CPU::getId()
{
    return this->_id;
}

String CPU::getVendor()
{
    return this->_vendor;
}

long CPU::getFrequency()
{
    return this->_frequency;
}

Json CPU::toJSON()
{
    Json cpu = Json::make_object();

    cpu.set("id", getId());
    cpu.set("vendor", getVendor());
    cpu.set("frequency", getFrequency());

    return cpu;
}

/******************************
 * NIC
 ******************************/
Json NIC::toJSON(bool stats)
{
    Json nic = Json::make_object();

    nic.set("id",getId());
    if (!stats) {
        nic.set("vendor", callRead("vendor"));
        nic.set("driver", callRead("driver"));
        nic.set("speed", callRead("speed"));
        nic.set("status", callRead("carrier"));
        nic.set("portType", callRead("type"));
        nic.set("hwAddr", callRead("mac").replace('-',':'));
        // TODO: Support VLAN and MPLS
        Json jtagging = Json::make_array();
        jtagging.push_back("mac");
        nic.set("rxFilter", jtagging);
    } else {
        nic.set("rxCount", callRead("hw_count"));
        nic.set("rxBytes", callRead("hw_bytes"));
        nic.set("rxDropped", callRead("hw_dropped"));
        nic.set("rxErrors", callRead("hw_errors"));
        nic.set("txCount", callTxRead("hw_count"));
        nic.set("txBytes", callTxRead("hw_bytes"));
        nic.set("txErrors", callTxRead("hw_errors"));
    }

    return nic;
}

String NIC::callRead(String h)
{
    const Handler *hC = Router::handler(element, h);

    if (hC && hC->visible()) {
        return hC->call_read(element, ErrorHandler::default_handler());
    }

    return "undefined";
}

String NIC::callTxRead(String h)
{
    // TODO: Ensure element type
    ToDPDKDevice *td = dynamic_cast<FromDPDKDevice *>(element)->findOutputElement();
    if (!td) {
        return "Could not find matching ToDPDKDevice!";
    }

    const Handler *hC = Router::handler(td, h);
    if (hC && hC->visible()) {
        return hC->call_read(td, ErrorHandler::default_handler());
    }

    return "undefined";
}

String NIC::getDeviceId()
{
    return callRead("device");
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)

EXPORT_ELEMENT(Metron)
ELEMENT_MT_SAFE(Metron)
