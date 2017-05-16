#include <click/config.h>

#include "metron.hh"

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


CLICK_DECLS


Metron::Metron() : _timing_stats(true) {

}

Metron::~Metron() {

}


int Metron::configure(Vector<String> &conf, ErrorHandler *errh) {
    Vector<Element*> nics;
    if (Args(conf, this, errh)
        .read_mp("ID", _id)
        .read_all("NIC",nics)
        .read_all("SLAVE_DPDK_ARGS",_dpdk_args)
        .read_all("SLAVE_ARGS",_args)
        .read("TIMING_STATS", _timing_stats)
        .complete() < 0)
        return -1;

    for (Element* e : nics) {
        NIC nic;
        nic.element = e;
        _nics.insert(nic.getId(), nic);
    }
    return 0;
}

static String parseHwInfo(String hwInfo, String key) {
    String s;
    s = hwInfo.substring(hwInfo.find_left(key) + key.length());
    int pos = s.find_left(':') + 2;
    s = s.substring(pos,s.find_left("\n") - pos);
    return s;
}
int Metron::initialize(ErrorHandler * errh) {
    _cpu_map.resize(getCpuNr(),0);
    String hwInfo = file_string("/proc/cpuinfo");
    _vendor = parseHwInfo(hwInfo,"vendor_id");
    _hw = parseHwInfo(hwInfo,"model name");
    _sw = CLICK_VERSION;
    String swInfo = shell_command_output_string("dmidecode -t 1","",errh);
    _serial = parseHwInfo(swInfo,"Serial Number");
	return 0;
}

void Metron::cleanup(CleanupStage) {
    auto begin = _scs.begin();
    while (begin != _scs.end()) {
        delete begin.value();
    }
}

int Metron::getAssignedCpuNr() {
    int tot = 0;
    for (int i = 0; i < getCpuNr(); i++) {
        if (_cpu_map[i] != 0) {
            tot++;
        }
    }
    return tot;
}

bool Metron::assignCpus(ServiceChain* sc, Vector<int>& map) {
    int j = 0;
    if (this->getAssignedCpuNr() + sc->getCpuNr() > this->getCpuNr()) {
        return false;
    }
    for (int i = 0; i < getCpuNr(); i++) {
        if (_cpu_map[i] == 0) {
            _cpu_map[i] = sc;
            map[j++] = i;
            if (j == sc->getCpuNr())
                return true;
        }
    }
    return false;
}

void Metron::unassignCpus(ServiceChain* sc) {
    int j = 0;
    for (int i = 0; i < getCpuNr(); i++) {
        if (_cpu_map[i] == sc) {
            _cpu_map[i] = 0;
        }
    }
}


String Metron::read_handler(Element *e, void *user_data) {
	Metron *m = static_cast<Metron *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    Json jroot = Json::make_object();

    switch (what) {
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

int ServiceChain::RxFilter::apply(NIC* nic, Bitvector cpus, ErrorHandler* errh) {
    //Only mac supported for now, only thing to do is to get addr
    Json jaddrs = Json::parse(nic->callRead("vf_mac_addr"));
    for (int i = 0; i < cpus.size(); i++) {
        if (!cpus[i])
            continue;
         if (atoi(nic->callRead("nb_vf_pools").c_str()) < i) {
             return errh->error("Not enough VF pools");
         }
         addr.push_back(jaddrs.get_s(String(i)));
    }
    return 0;

}

ServiceChain* Metron::findChainById(String id) {
    return _scs.find(id);
}

/**
 * Assign cpus to the chain and run it. If it succeed, the chain is added
 * to the internal chains list. On failure, cpus are unassigned.
 * It is the responsability of the caller to delete the chain on error.
 */
int Metron::instanciateChain(ServiceChain* sc, ErrorHandler *errh) {
    if (!assignCpus(sc,sc->getCpuMapRef())) {
        errh->error("Could not assign enough CPUs");
        return -1;
    }
    int ret = runChain(sc, errh);
    if (ret == 0) {
        sc->status = ServiceChain::SC_OK;
        _scs.insert(sc->getId(),sc);
        return 0;
    } else {
        unassignCpus(sc);
        return -1;
    }
}

/**
 * Run a service chain and keep a control socket to it
 *
 * CPUs must be already assigned.
 */
int Metron::runChain(ServiceChain* sc, ErrorHandler *errh) {
    for (int i = 0; i < sc->nic.size(); i++) {
        if (sc->rxFilter->apply(sc->nic[i],sc->assignedCpus(),errh) != 0) {
            return errh->error("Could not apply RX filter");
        }
    }
    int configpipe[2], ctlsocket[2];
    if (pipe(configpipe) == -1)
        return errh->error("Could not create pipe");
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, ctlsocket) == -1)
        return errh->error("Could not create socket");

    //Launch slave
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

        char* argv_char[argv.size() + 1];
        for (int i = 0; i < argv.size(); i++) {
            argv_char[i] = strdup(argv[i].c_str());
        }
        argv_char[argv.size()] = 0;
        if ((ret = execv(argv_char[0], argv_char))) {
            click_chatter("Could not launch slave process : %d %d",ret,errno);
        }
        exit(1);
    } else {
        click_chatter("Child %d launched successfully",pid);
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
        click_chatter("Writing config %s",conf.c_str());

        int pos = 0;
        while (pos != conf.length()) {
            ssize_t r = write(configpipe[1], conf.begin() + pos, conf.length() - pos);
            if (r == 0 || (r == -1 && errno != EAGAIN && errno != EINTR)) {
                if (r == -1)
                errh->message("%s while writing configuration", strerror(errno));
                break;
            } else if (r != -1)
                pos += r;
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
            return errh->error("Could not read from control socket : error %d",v);
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
 * Stop and remove a chain from the internal list, then unassign cpus.
 * It is the responsability of the caller to delete the chain.
 */
int Metron::removeChain(ServiceChain* sc, ErrorHandler*) {
    sc->controlSendCommand("WRITE stop");
    _scs.remove(sc->getId());
    unassignCpus(sc);
    return 0;
}

int Metron::write_handler( const String &data, Element *e, void *user_data, ErrorHandler *errh) {
    Metron *m = static_cast<Metron *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    switch (what) {
        case h_delete_chains: {
            ServiceChain* sc = m->findChainById(data);
            if (sc == 0) {
                return errh->error("Unknown ID %s",data.c_str());
            }
            int ret = m->removeChain(sc,errh);
            if (ret == 0)
                delete(sc);
            return ret;
        }
    }
    return -1;
}


int
Metron::param_handler(int operation, String &param, Element *e, const Handler * h, ErrorHandler *errh)
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
                    ServiceChain* sc = m->findChainById(param);
                    if (!sc) {
                        return errh->error("Unkown ID %s", param.c_str());
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
                    ServiceChain* sc = m->findChainById(param);
                    if (!sc) {
                        return errh->error("Unkown ID %s", param.c_str());
                    }
                    jroot = sc->statsToJSON();
                }
                break;
            }
            case h_chains_proxy: {
                int pos = param.find_left("/");
                if (pos <= 0) {
                    param = "You must give an id then a command";
                    return 0;
                }
                String ids = param.substring(0, pos);
                ServiceChain* sc = m->findChainById(ids);
                if (!sc) {
                    return errh->error("Unknown ID %s",ids.c_str());
                }
                param = sc->callRead(param.substring(pos + 1));
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
                    ServiceChain* sc = ServiceChain::fromJSON(jsc.second,m,errh);
                    if (m->_timing_stats) {
                        ts.parse = Timestamp::now_steady();
                    }
                    if (!sc) {
                        return errh->error("Could not instantiate a chain");
                    }
                    if (m->findChainById(sc->id) != 0) {
                        delete sc;
                        return errh->error("A chain with this ID already exists. Delete it first.");
                    }
                    int ret = m->instanciateChain(sc, errh);
                    if (ret != 0) {
                        delete sc;
                        return errh->error("Could not start the chain");
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

void Metron::add_handlers() {
	add_read_handler("resources", read_handler, h_resources);
	add_read_handler("stats", read_handler, h_stats);
	add_write_handler("delete_chains", write_handler, h_delete_chains);

    set_handler("chains", Handler::f_write | Handler::f_read | Handler::f_read_param, param_handler, h_chains, h_chains);
    set_handler("chains_stats", Handler::f_read | Handler::f_read_param, param_handler, h_chains_stats);
    set_handler("chains_proxy", Handler::f_write | Handler::f_read | Handler::f_read_param, param_handler, h_chains_proxy);
}

Json Metron::toJSON() {
    Json jroot = Json::make_object();
    jroot.set("id",Json(_id));

    //Cpu ressources
    jroot.set("cpus",Json(getCpuNr()));

    //Infos
    jroot.set("manufacturer",Json(_vendor));
    jroot.set("hwVersion",Json(_hw));
    jroot.set("swVersion",Json("Click "+_sw));
    jroot.set("serial",Json(_serial));

    //Nics ressources
    Json jnics = Json::make_array();
    auto begin = _nics.begin();
    while (begin != _nics.end()) {
        jnics.push_back(begin.value().toJSON(false));
        begin++;
    }
    jroot.set("nics",jnics);
    return jroot;
}

Json Metron::statsToJSON() {
    Json jroot = Json::make_object();

    //Cpu ressources
    jroot.set("busyCpus",Json(getAssignedCpuNr()));
    jroot.set("freeCpus",Json(getCpuNr() - getAssignedCpuNr()));

    //Nics ressources
    Json jnics = Json::make_array();
    auto begin = _nics.begin();
    while (begin != _nics.end()) {
        jnics.push_back(begin.value().toJSON(true));
        begin++;
    }
    jroot.set("nics",jnics);
    return jroot;
}

/***************************************
 * RxFilter
 **************************************/
ServiceChain::RxFilter* ServiceChain::RxFilter::fromJSON(Json j, ServiceChain* sc, ErrorHandler* errh) {
    ServiceChain::RxFilter* rf = new RxFilter(sc);
    rf->method = j.get_s("method").lower();
    if (rf->method != "mac") {
        errh->error("Unsupported RX Filter method : %s",rf->method.c_str());
        return 0;
    }
    Json jaddrs = j.get("values");
    for (int i = 0; i < jaddrs.size(); i++) {
        rf->addr.push_back(jaddrs.get_s(String(i)));
    }
    return rf;
}

Json ServiceChain::RxFilter::toJSON() {
    Json j;
    j.set("method",method);
    Json jaddr = Json::make_array();
    for (int i = 0; i < addr.size(); i++) {
        jaddr.push_back(addr[i]);
    }
    j.set("values",jaddr);
    return j;
}

/************************
 * Service Chain
 ************************/
ServiceChain* ServiceChain::fromJSON(Json j, Metron* m, ErrorHandler* errh) {
    ServiceChain* sc = new ServiceChain(m);
    sc->id = j.get_s("id");
    if (sc->id == "") {
        sc->id = String(m->getNbChains());
    }
    sc->rxFilter = ServiceChain::RxFilter::fromJSON(j.get("rxFilter"), sc, errh);
    sc->config = j.get_s("config");
    sc->cpu_nr = j.get_i("cpus");
    sc->_cpus.resize(sc->cpu_nr);
    Json jnics = j.get("nics");
    for (auto jnic : jnics) {
        NIC* nic = m->_nics.findp(jnic.second.as_s());
        if (!nic) {
            errh->error("Unknown NIC : %s",jnic.second.as_s().c_str());
            delete sc;
            return 0;
        }
        sc->nic.push_back(nic);
    }
    return sc;
}

Json ServiceChain::toJSON() {
    Json jsc = Json::make_object();
    jsc.set("id",getId());
    jsc.set("rxFilter",rxFilter->toJSON());
    jsc.set("config",config);
    jsc.set("expanded_config", generateConfig());
    Json jcpus = Json::make_array();
    for (int i = 0; i < getCpuNr(); i++) {
        jcpus.push_back(getCpuMap(i));
    }
    jsc.set("cpus", jcpus);
    jsc.set("status", status);
    Json jnics = Json::make_array();
    for (auto n : nic) {
        jnics.push_back(Json::make_string(n->getId()));
    }
    jsc.set("nics",jnics);
    return jsc;
}

Json ServiceChain::statsToJSON() {
    Json jsc = Json::make_object();
    jsc.set("id",getId());

    Json jcpus = Json::make_array();
    for (int j = 0; j < getCpuNr(); j ++) {
        String js = String(j);
        int avg_max = 0;
        for (int i = 0; i < nic.size(); i++) {
            String is = String(i);
            int avg = atoi(callRead( "batchAvg"+is+ "C"+js+".average").c_str());
            if (avg > avg_max)
                avg_max = avg;
        }
        Json jcpu = Json::make_object();
        jcpu.set("id", j);
        jcpu.set("load", (float)avg_max / 32.0f);
        jcpus.push_back(jcpu);
    }
    jsc.set("cpus", jcpus);

    Json jnics = Json::make_array();
    for (int i = 0; i < nic.size(); i++) {
        String is = String(i);
        uint64_t rx_count = 0;
        uint64_t rx_bytes = 0;
        uint64_t rx_dropped = 0;
        uint64_t rx_errors = 0;
        uint64_t tx_count = 0;
        uint64_t tx_bytes = 0;
        uint64_t tx_dropped = 0;
        uint64_t tx_errors = 0;
        for (int j = 0; j < getCpuNr(); j ++) {
            String js = String(j);
            rx_count += atol(callRead( "slaveFD"+is+ "C"+js+".count").c_str());
            //rx_bytes += atol(callRead( "slaveFD"+is+ "C"+js+".bytes").c_str());
            //rx_dropped += atol(callRead( "slaveFD"+is+ "C"+js+".dropped").c_str());
            //rx_errors += atol(callRead( "slaveFD"+is+ "C"+js+".errors").c_str());

        }
        tx_count += atol(callRead( "slaveTD"+is+ ".count").c_str());
        //tx_bytes += atol(callRead( "slaveTD"+is+ ".bytes").c_str());
        tx_dropped += atol(callRead( "slaveTD"+is+ ".dropped").c_str());
        //tx_errors += atol(callRead( "slaveTD"+is+ ".errors").c_str());
        Json nic = Json::make_object();
        nic.set("rxCount",rx_count);
        nic.set("rxBytes",rx_bytes);
        nic.set("rxDropped",rx_dropped);
        nic.set("rxErrors",rx_errors);
        nic.set("txCount",tx_count);
        nic.set("txBytes",tx_bytes);
        nic.set("txDropped",tx_dropped);
        nic.set("txErrors",tx_errors);
        jnics.push_back(nic);
    }
    jsc.set("nics",jnics);
    if (_metron->_timing_stats) {
        jsc.set("timing_stats", _timing_stats.toJSON());
    }
    return jsc;
}

Json ServiceChain::timing_stats::toJSON() {
    Json j = Json::make_object();
    j.set("parse",(parse-start).nsecval());
    j.set("launch",(launch-parse).nsecval());
    j.set("total",(launch-start).nsecval());
    return j;
}

String ServiceChain::generateConfig()
{
    String newconf = "elementclass MetronSlave {\n" + config + "\n};\n\n";
    newconf += "slave :: MetronSlave();\n\n";
    for (int i = 0; i < nic.size(); i++) {
       String is = String(i);
       for (int j = 0; j < cpu_nr; j++) {
           String js = String(j);
           int cpuid = _cpus[j];
           int queue_no = rxFilter->cpuToQueue(nic[i],cpuid);
           newconf += "slaveFD"+is+ "C"+js+" :: "+nic[i]->element->class_name()+"("+nic[i]->getDeviceId()+",QUEUE "+String(queue_no)+", N_QUEUES 1,THREADOFFSET " +String(cpuid)+ ", MAXTHREADS 1, BURST 32, NUMA false, VERBOSE 99);\n";
           newconf += "slaveFD"+is+ "C"+js+" -> batchAvg"+is+ "C"+js+ " :: AverageBatchCounter() -> [" + is + "]slave;\n";
        //   newconf += "Script(label s, read batchAvg"+is+ "C"+js+ ".average, wait 1s, goto s);\n";
       }

       newconf += "slaveTD"+is+ " :: ToDPDKDevice("+nic[i]->getDeviceId()+");"; //TODO : allowed CPU bitmap
       newconf += "slave[" + is + "] -> slaveTD" + is + ";\n";

    }
    return newconf;
}

Vector<String> ServiceChain::buildCmdLine(int socketfd) {
    Vector<String> argv;
    int i;

    String cpulist = "";


    for (int i = 0; i < click_max_cpu_ids(); i++) {
        cpulist += String(i) + (i < click_max_cpu_ids() -1? ",":"");
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
        click_chatter("ARG %s",argv[i].c_str());
    }
    return argv;
}


Bitvector ServiceChain::assignedCpus() {
    Bitvector b;
    b.resize(_metron->_cpu_map.size());
    for (int i = 0; i < b.size(); i ++) {
        b[i] = _metron->_cpu_map[i] == this;
    }
    return b;
}

void ServiceChain::checkAlive() {
    if (kill(_pid, 0) != 0) {
        _metron->removeChain(this, ErrorHandler::default_handler());
    } else {
        click_chatter("Pid %d is alive", _pid);
    }
}

void ServiceChain::controlInit(int fd, int pid) {
    _socket = fd;
    _pid = pid;
}

int ServiceChain::controlReadLine(String& line) {
    char buf[1024];
    int n = read(_socket, &buf, 1024);
    if (n <= 0)
        return n;
    line = String(buf);
    while (n == 1024) {
        n = read(_socket, &buf, 1024);
        line += String(buf);
    }
    return line.length();
}

void ServiceChain::controlWriteLine(String cmd) {
    int n = write(_socket,(cmd + "\r\n").c_str(),cmd.length() + 1);
}

String ServiceChain::controlSendCommand(String cmd) {
    controlWriteLine(cmd);
    String ret;
    controlReadLine(ret);
    return ret;
}

String ServiceChain::callRead(String handler) {
    String ret = controlSendCommand("READ " + handler);
    if (ret == "") {
        checkAlive();
        return "";
    }
    int code = atoi(ret.substring(0, 3).c_str());
    if (code >= 500)
        return ret.substring(4);
    ret = ret.substring(ret.find_left("\r\n") + 2);
    assert(ret.starts_with("DATA "));
    ret = ret.substring(5);
    int eof = ret.find_left("\r\n");
    int n = atoi(ret.substring(0, eof).c_str());
    ret = ret.substring(3, n);
    return ret;
}

/******************************
 * NIC
 ******************************/
Json NIC::toJSON(bool stats) {
    Json nic = Json::make_object();
    nic.set("id",getId());
    if (!stats) {
        nic.set("speed",callRead("speed"));
        nic.set("status",callRead("carrier"));
        nic.set("portType",callRead("type"));
        nic.set("hwAddr",callRead("mac").replace('-',':'));
        Json jtagging = Json::make_array();
        //jtagging.push_back("vlan"); TODO : support
        jtagging.push_back("mac");
        nic.set("rxFilter",jtagging);
    } else {
        nic.set("rxCount",callRead("hw_count"));
        nic.set("rxBytes",callRead("hw_bytes"));
        nic.set("rxDropped",callRead("hw_dropped"));
        nic.set("rxErrors",callRead("hw_errors"));
        nic.set("txCount",callTxRead("hw_count"));
        nic.set("txBytes",callTxRead("hw_bytes"));
        nic.set("txErrors",callTxRead("hw_errors"));
    }
    return nic;
}

String NIC::callRead(String h) {
    const Handler* hC = Router::handler(element, h);
    if (hC && hC->visible()) {
        return hC->call_read(element, ErrorHandler::default_handler());
    }
    return "undefined";
}

String NIC::callTxRead(String h) {
    //TODO : ensure elem type
    ToDPDKDevice* td = dynamic_cast<FromDPDKDevice*>(element)->findOutputElement();
    if (!td) {
        return "Could not find matching ToDPDKDevice !";
    }

    const Handler* hC = Router::handler(td, h);
    if (hC && hC->visible()) {
        return hC->call_read(td, ErrorHandler::default_handler());
    }
    return "undefined";
}

String NIC::getDeviceId() {
    return callRead("device");
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)

EXPORT_ELEMENT(Metron)
ELEMENT_MT_SAFE(Metron)
