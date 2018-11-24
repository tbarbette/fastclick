// -*- c-basic-offset: 4; related-file-name: "servicechain.hh" -*-
/*
 * servicechain.{cc,hh} -- library to manage various types of service chains,
 * including Click-based and standalone ones.
 *
 * Copyright (c) 2018 Tom Barbette, KTH Royal Institute of Technology
 * Copyright (c) 2018 Georgios Katsikas, KTH Royal Institute of Technology
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

#include <click/string.hh>
#include <click/args.hh>
#include <metron/servicechain.hh>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <stdio.h>

#include "../elements/userlevel/blackboxnf.hh"

/****************************************
 * Service Chain Manager
 ****************************************/
ServiceChainManager::ServiceChainManager(ServiceChain *sc)
{
    _sc = sc;
    assert(_sc);
}

ServiceChainManager::~ServiceChainManager()
{
}

/****************************************
 * Click Service Chain Manager
 ****************************************/
/**
 * Fix rule sent by the controller according to dataplane informations
 */
String
ServiceChainManager::fix_rule(NIC *nic, String rule) {
    // compose rule for the right nic
    rule = "flow create " + String(nic->get_port_id()) + " " + rule;
    return rule;
}

/**
 * Kill a Click-based service chain.
 */
void
ClickSCManager::kill_service_chain()
{
    metron()->_command_lock.acquire();
    control_send_command("WRITE stop");
    metron()->_command_lock.release();
}

/**
 * Run a service chain and keep a control socket to it.
 * CPU cores must already be assigned by assign_cpus().
 */
int
ClickSCManager::run_service_chain(ErrorHandler *errh)
{
    ServiceChain *sc = _sc;

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

        Vector<String> argv = build_cmd_line(ctl_socket[1]);

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
        String conf = sc->generate_configuration(add_extra);

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
            control_init(ctl_socket[0], pid);
        }

        String s;
        int v = control_read_line(s);
        if (v <= 0) {
            return errh->error("Could not read from control socket: Error %d", v);
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
 * Associates a CPU core with a service chain.
 */
int
ClickSCManager::activate_core(int new_cpu_id, ErrorHandler *errh)
{
    int ret = 0;
    String response = "";
    for (int inic = 0; inic < metron()->get_nics_nb(); inic++) {
        ret = call_write(
            _sc->generate_configuration_slave_fd_name(
                inic, _sc->get_cpu_phys_id(new_cpu_id)
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
        if (metron()->_rx_mode == RSS) {
            _sc->_nics[inic]->call_rx_write("max_rss", String(new_cpu_id + 1));
        }
    }
    return ret;
};

/**
 * Tears down a CPU core associated with a service chain.
 */
int
ClickSCManager::deactivate_core(int new_cpu_id, ErrorHandler *errh)
{
    String response = "";
    int ret = 0;
    for (int inic = 0; inic < _sc->get_nics_nb(); inic++) {
        // Stop using the new cores BEFORE the secondary has been torn down
        if (metron()->_rx_mode == RSS) {
            _sc->_nics[inic]->call_rx_write("max_rss", String(new_cpu_id + 1));
        }
        int ret = call_write(
                _sc->generate_configuration_slave_fd_name(
                inic, _sc->get_cpu_phys_id(new_cpu_id)
            ) + ".safe_active", response, "0"
        );
        if ((ret < 200) || (ret >= 300)) {
            return errh->error(
                "Response to activate input was %d: %s",
                ret, response.c_str()
            );
        }
    }
    return ret;
}

/**
 * Initializes the control socket of a service chain.
 */
void
ClickSCManager::control_init(int fd, int pid)
{
    _socket = fd;
    _pid = pid;
}

/**
 * Reads a control message from the control socket of a service chain.
 */
int
ClickSCManager::control_read_line(String &line)
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
ClickSCManager::control_write_line(String cmd)
{
    int n = write(_socket, (cmd + "\r\n").c_str(), cmd.length() + 1);
}

/**
 * Passes a control message through the control socket and gets
 * a response.
 */
String
ClickSCManager::control_send_command(String cmd)
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
ClickSCManager::call(
        String fnt, bool has_response, String handler,
        String &response, String params)
{
    metron()->_command_lock.acquire();
    String ret = control_send_command(fnt + " " + handler + (params? " " + params : ""));
    if (ret.empty()) {
        check_alive();
        metron()->_command_lock.release();
        return ERROR;
    }

    int code = atoi(ret.substring(0, 3).c_str());
    if (code >= 500) {
        response = ret.substring(4);
        metron()->_command_lock.release();
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

    metron()->_command_lock.release();
    return code;
}

/**
 * Implements simple read handlers for a service chain.
 */
String
ClickSCManager::simple_call_read(String handler)
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
ClickSCManager::simple_call_write(String handler)
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
ClickSCManager::call_read(String handler, String &response, String params)
{
    return call("READ", true, handler, response, params);
}

/**
 * Implements write handlers for a service chain.
 */
int
ClickSCManager::call_write(String handler, String &response, String params)
{
    return call("WRITE", false, handler, response, params);
}

/**
 * Returns NIC statistics related to a service chain in JSON format.
 */
Json
ClickSCManager::nic_stats_to_json()
{
    Json jnics = Json::make_array();

    for (int i = 0; i < _sc->get_nics_nb(); i++) {
        String is = String(i);
        uint64_t rx_count   = 0;
        uint64_t rx_bytes   = 0;
        uint64_t rx_dropped = 0;
        uint64_t rx_errors  = 0;
        uint64_t tx_count   = 0;
        uint64_t tx_bytes   = 0;
        uint64_t tx_dropped = 0;
        uint64_t tx_errors  = 0;

        for (int j = 0; j < _sc->get_max_cpu_nb(); j++) {
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
        jnic.set("name",      _sc->get_nic_by_index(i)->get_name());
        jnic.set("index",     _sc->get_nic_by_index(i)->get_index());
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

    return jnics;
}

/**
 * Generates the necessary DPDK arguments for the deployment
 * of a service chain as a secondary DPDK process.
 */
Vector<String>
ClickSCManager::build_cmd_line(int socketfd)
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

    for (i = 0; i < metron()->_dpdk_args.size(); i++) {
        argv.push_back(metron()->_dpdk_args[i]);
    }
    argv.push_back("--");
    argv.push_back("--socket");
    argv.push_back(String(socketfd));
    for (i = 0; i < metron()->_args.size(); i++) {
        argv.push_back(metron()->_args[i]);
    }

    for (i = 0; i < argv.size(); i++)  {
        click_chatter("ARG %s", argv[i].c_str());
    }
    return argv;
}

/**
 * Auto-scales a Click-based service chain.
 */
void
ClickSCManager::do_autoscale(ErrorHandler *errh)
{
    String response;
    int ret = call_write("slave/rrs.max", response, String(_sc->get_active_cpu_nb()));
    if ((ret < 200) || (ret >= 300)) {
        errh->error(
            "Response to change the number of CPU core %d: %s",
            ret, response.c_str()
        );
        return;
    }
};

/**
 * Issues a read command ro a service chain's control socket.
 */
String
ClickSCManager::command(String cmd)
{
    return simple_call_read(cmd);
};

/**
 * Runs a monitoring task for a service chain.
 */
void
ClickSCManager::run_load_timer()
{
    double alpha_up = 0.5;
    double alpha_down = 0.3;
    double total_alpha = 0.5;

    float max_cpu_load = 0;
    int max_cpu_load_index = 0;
    float total_cpu_load = 0;

    Vector<String> min = simple_call_read("monitoring_lat.mp_min").split(' ');
    Vector<String> max = simple_call_read("monitoring_lat.mp_max").split(' ');
    Vector<String> avg = simple_call_read("monitoring_lat.mp_average_time").split(' ');
    simple_call_write("monitoring_lat.reset");
    Vector<String> load = simple_call_read("load").split(' ');

    for (int j = 0; j < _sc->get_max_cpu_nb(); j++) {

        const int cpu_id = _sc->get_cpu_phys_id(j);
        String js = String(j);
        float cpu_load = 0;
        float cpu_queue = 0;
        uint64_t throughput = 0;
        for (int i = 0; i < _sc->get_nics_nb(); i++) {
            String is = String(i);
            NIC *nic = _sc->get_nic_by_index(i);
            assert(nic);
            int stat_idx = (j * _sc->get_nics_nb()) + i;

            String name = _sc->generate_configuration_slave_fd_name(i, cpu_id);
//                long long useless = atoll(simple_call_read(name + ".useless").c_str());
//                long long useful = atoll(simple_call_read(name + ".useful").c_str());
            long long count = atoll(simple_call_read(name + ".count").c_str());
//                long long useless_diff = useless - sc->nic_stats[stat_idx].useless;
//                long long useful_diff = useful - sc->nic_stats[stat_idx].useful;
            long long count_diff = count - _sc->_nic_stats[stat_idx].count;
//                sc->nic_stats[stat_idx].useless = useless;
//                sc->nic_stats[stat_idx].useful = useful;
            _sc->_nic_stats[stat_idx].count = count;
//                long long count = atoll(simple_call_write(name + ".reset_load").c_str());
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
            //sc->nic_stats[stat_idx].load = simple_call_read(
            // click_chatter(
            //      "[SC %d] Load NIC %d CPU %d - %f %f - diff usefull %lld useless %lld",
            //      sn, i, j, load, sc->nic_stats[stat_idx].load, useful_diff, useless_diff
            //  );

           /* if (sc->nic_stats[stat_idx].load > cpu_load) {
                cpu_load = sc->nic_stats[stat_idx].load;
            }*/
            throughput += atoll(simple_call_read("monitoring_th_" + is + "_" + js + ".link_rate").c_str());
            assert(_sc->get_cpu_info(j).assigned());
            float ncpuqueue = (float)atoi(simple_call_read(name + ".queue_count "+String(nic->phys_cpu_to_queue(_sc->get_cpu_phys_id(j)))).c_str()) / (float)(atoi(nic->call_tx_read("nb_rx_desc").c_str()));
            if (ncpuqueue > cpu_queue) {
                cpu_queue = ncpuqueue;
            }
        }
        cpu_load = atof(load[j].c_str());
        _sc->_cpus[j].load = cpu_load;
        _sc->_cpus[j].max_nic_queue = cpu_queue;
        if (metron()->_monitoring_mode) {
            _sc->_cpus[j].latency.avg_throughput = throughput;
            _sc->_cpus[j].latency.min_latency = atoll(min[j].c_str());
            _sc->_cpus[j].latency.max_latency = atoll(max[j].c_str());
            _sc->_cpus[j].latency.average_latency = atoll(avg[j].c_str());
        }
        total_cpu_load += cpu_load;
        if (cpu_load > max_cpu_load) {
           max_cpu_load = cpu_load;
           max_cpu_load_index = j;
        }
    }

    if (_sc->_autoscale) {
        _sc->_total_cpu_load = _sc->_total_cpu_load *
                              (1 - total_alpha) + max_cpu_load * (total_alpha);
        if (_sc->_total_cpu_load > metron()->CPU_OVERLOAD_LIMIT) {
            _sc->do_autoscale(1);
        } else if (_sc->_total_cpu_load < metron()->CPU_UNDERLOAD_LIMIT) {
            _sc->do_autoscale(-1);
        }
    } else {
        _sc->_total_cpu_load = _sc->_total_cpu_load * (1 - total_alpha) +
                              (total_cpu_load / _sc->get_active_cpu_nb()) * (total_alpha);
    }
    _sc->_max_cpu_load = max_cpu_load;
    _sc->_max_cpu_load_index = max_cpu_load_index;
}


/****************************************
 * PID-based Service Chain Manager
 ****************************************/
/**
 * Checks whether a service chain is alive or not.
 */
void
PidSCManager::check_alive()
{
    if (_pid >0) {
        if (kill(_pid, 0) != 0) {
            metron()->delete_service_chain(_sc, ErrorHandler::default_handler());
        } else {
            click_chatter("Error: PID %d is still alive", _pid);
        }
    }
}

/****************************************
 * Standalone Service Chain Manager
 ****************************************/
/**
 * Fix the rule sent by the controller according to data plane information.
 */
String
StandaloneSCManager::fix_rule(NIC *nic, String rule) {
    // Compose rule for the right NIC
    if (_sriov < 0) {
        rule = "flow create " + String(nic->get_port_id()) + " " + rule;
    } else {
        int pindex = nic->get_port_id() + 1;
        rule = "flow create " + String(nic->get_port_id()) + " transfer " + rule;
        int pos = rule.find_left("queue index ");
        String qid = rule.substring(pos + 12);
        //click_chatter("qid %s", qid.c_str());
        int queue = atoi(qid.substring(0, qid.find_left(' ')).c_str());
        rule = rule.substring(0,pos) + "port_id id " + String(pindex + queue % _sriov) + " / end\n";
        //click_chatter("%s", rule.c_str());
    }

    return rule;
}

/**
 * Kills a standalone service chain.
 */
void
StandaloneSCManager::kill_service_chain()
{
#if HAVE_BATCH
    if (_pid > 0)
        kill(_pid, SIGKILL);
#else
    click_chatter("Standalone service chains can only be killed in batch mode");
    return;
#endif
}

/**
 * Run a standalone service chain
 */
int
StandaloneSCManager::run_service_chain(ErrorHandler *errh)
{
#if HAVE_BATCH
    ServiceChain *sc = _sc;

    for (int i = 0; i < sc->get_nics_nb(); i++) {
        if (sc->rx_filter->apply(sc->get_nic_by_index(i), errh) != 0) {
            return errh->error("Could not apply Rx filter");
        }
    }

    String exec;
    String args = "";
    Vector<String> conf;
    cp_argvec(sc->config, conf);
    if (Args(conf, errh)
        .read_mp("EXEC", exec)
        .read("ARGS", args)
        .read("SRIOV", _sriov)
        .consume() < 0) {
        return errh->error("Could not parse configuration string!");
    }

    if (_sriov >= 0) {
    #if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        int idx = 0;
        for (int i = 0; i < sc->get_nics_nb(); i++) {
            // Insert VF->PF traffic return rules
            HashMap<long, String> rules_map;

            NIC *nic = sc->get_nic_by_index(i);

            int pindex = nic->get_port_id();

            rules_map.insert(0, "flow create " + String(_sriov + pindex) + " transfer ingress pattern eth type is 2048 / end actions port_id id " + String(pindex) + " / end\n");
            int status = nic->get_flow_director(_sriov)->add_rules(rules_map, false);
            if (status < 0) {
                return errh->error("Could not insert SRIOV revert rule");
            }
        }
        /*
for (int i = 0; i < sc->get_nics_nb(); i++) {
            // Insert VF->PF traffic return rules
            HashMap<long, String> rules_map;

            NIC *nic = sc->get_nic_by_index(i);

            int pindex = nic->get_port_id();

            rules_map.insert(0, "flow create 0 transfer ingress pattern eth type is 2048 / ipv4 src spec 49.192.0.0 src prefix 10 dst spec 223.0.0.0 dst prefix 10 / end actions port_id id 1 / end\n");
            int status = nic->get_flow_director(pindex)->add_rules(rules_map, false);
            if (status < 0) {
                return errh->error("Could not insert SRIOV revert rule");
            }



        }*/
    #else
        return errh->error("SRIOV rules are not supported by this DPDK version");
    #endif
    }

    Bitvector cpu;
    cpu.resize(click_max_cpu_ids());
    for (int j = 0; j < sc->get_max_cpu_nb(); j++) {
        assert(sc->get_cpu_info(j).assigned());
        cpu[sc->get_cpu_phys_id(j)] = true;
    }

    if (exec == "" || exec == "0") {
        click_chatter("Launching a ghost service chain");
        _pid = -1;
        return SUCCESS;
    }

    int pid = BlackboxNF::run_slave(exec, args, true, cpu, "");
    if (pid <= 0) {
        return ERROR;
    } else {
        _pid = pid;
    }
    return SUCCESS;
#else
    return errh->error("Standalone service chains can only run in batch mode");
#endif
}
