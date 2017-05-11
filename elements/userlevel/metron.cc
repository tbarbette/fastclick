#include <click/config.h>

#include "metron.hh"

#include <click/args.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/userutils.hh>
#include <sys/ioctl.h>
#include <sys/fcntl.h>



CLICK_DECLS


Metron::Metron() {

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

Bitvector Metron::ServiceChain::assignedCpus() {
    Bitvector b;
    b.resize(_metron->_cpu_map.size());
    for (int i = 0; i < b.size(); i ++) {
        b[i] = _metron->_cpu_map[i] == this;
    }
    return b;
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
        case h_chains: {
            //NICs ressources
            Json jscs = Json::make_array();
            auto begin = m->_scs.begin();
            while (begin != m->_scs.end()) {
                jscs.push_back(begin.value()->toJSON());
                begin++;
            }
            jroot.set("servicechains",jscs);
            break;
        }
    }
    return jroot.unparse(true);
}

int Metron::addChain(ServiceChain* sc, ErrorHandler *errh) {
    for (int i = 0; i < sc->nic.size(); i++) {
        sc->rxFilter->apply(sc->nic[i],sc->assignedCpus());
    }
    _scs.insert(sc->getId(),sc);

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
        int fd = ctlsocket[0];
        if (ioctl(fd, FIONBIO, &flags) != 0) {
            flags = fcntl(fd, F_GETFL);
            if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
                return errh->error("%s", strerror(errno));
        }

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
        } else {
            close(configpipe[1]);
            /*GIOChannel *channel = g_io_channel_unix_new(ctlsocket[0]);
            g_io_channel_set_encoding(channel, NULL, NULL);
            new csocket_cdriver(this, channel, true);*/
        }
    }
    return 0;

}

int Metron::write_handler( const String &data, Element *e, void *user_data, ErrorHandler *errh) {
    Metron *m = static_cast<Metron *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    switch (what) {
        case h_chains:
            Json jroot = Json::parse(data);
            Json jlist = jroot.get("servicechains");
            for (auto jsc : jlist) {
                ServiceChain* sc = ServiceChain::fromJSON(jsc.second,m,errh);
                if (!sc) {
                    return errh->error("Could not instantiate a chain");
                }
                int ret = m->addChain(sc, errh);
                if (ret != 0) {
                    sc->status = ServiceChain::SC_FAILED;
                    return errh->error("Could not start the chain");
                } else {
                    sc->status = ServiceChain::SC_OK;
                }
            }
        return 0;
    }
    return -1;
}

/*
int
Metron::chains_handler(int, String &param, Element *e, const Handler *, ErrorHandler *errh)
{
    Metron *m = static_cast<Metron *>(e);

}*/

void Metron::add_handlers() {
	add_read_handler("resources", read_handler, h_resources);
	add_read_handler("stats", read_handler, h_stats);
	add_read_handler("chains", read_handler, h_chains);
	add_write_handler("chains", write_handler, h_chains);
	add_write_handler("delete_chains", write_handler, h_delete_chains);

	//set_handler("chains", f_write, delete_chain_handler);
}

/**
 * NIC
 */
Json Metron::NIC::toJSON(bool stats) {
    Json nic = Json::make_object();
    nic.set("id",getId());
    if (!stats) {
        nic.set("speed",callRead("speed"));
        nic.set("status",callRead("carrier"));
        nic.set("hwAddr",callRead("mac").replace('-',':'));
        Json jtagging = Json::make_array();
        //jtagging.push_back("vlan"); TODO : support
        jtagging.push_back("mac");
        nic.set("tagging",jtagging);
    } else {
        nic.set("rxCount",callRead("hw_count"));
        nic.set("rxBytes",callRead("hw_bytes"));
        nic.set("rxDropped",callRead("hw_dropped"));
        nic.set("rxErrors",callRead("hw_errors"));
        nic.set("txCount",callRead("tx_count"));
        nic.set("txBytes",callRead("tx_bytes"));
        nic.set("txErrors",callRead("tx_errors"));
    }
    return nic;
}

String Metron::NIC::callRead(String h) {
    const Handler* hC = Router::handler(element, h);
    if (hC && hC->visible()) {
        return hC->call_read(element, ErrorHandler::default_handler());
    }
    return "undefined";

}

String Metron::NIC::getDeviceId() {
    return callRead("device");
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

/**
 * RxFilter
 */
Metron::ServiceChain::RxFilter* Metron::ServiceChain::RxFilter::fromJSON(Json j, ServiceChain* sc, ErrorHandler* errh) {
    Metron::ServiceChain::RxFilter* rf = new RxFilter(sc);
    rf->method = j.get_s("method").lower();
    if (rf->method != "mac") {
        errh->error("Unsupported RX Filter method : %s",rf->method.c_str());
        return 0;
    }
    Json jaddrs = j.get("addr");
    for (int i = 0; i < jaddrs.size(); i++) {
        rf->addr.push_back(jaddrs.get_s(String(i)));
    }
    return rf;
}

Json Metron::ServiceChain::RxFilter::toJSON() {
    Json j;
    j.set("method",method);
    Json jaddr = Json::make_array();
    for (int i = 0; i < addr.size(); i++) {
        jaddr.push_back(addr[i]);
    }
    j.set("addr",jaddr);
    return j;
}

/**
 * Service Chain
 */
Metron::ServiceChain* Metron::ServiceChain::fromJSON(Json j, Metron* m, ErrorHandler* errh) {
    Metron::ServiceChain* sc = new ServiceChain(m);
    sc->id = j.get_s("id");
    sc->rxFilter = Metron::ServiceChain::RxFilter::fromJSON(j.get("rxFilter"), sc, errh);
    sc->config = j.get_s("config");
    sc->cpu_nr = j.get_i("cpus");
    Json jnics = j.get("nics");
    for (auto jnic : jnics) {
        Metron::NIC* nic = m->_nics.findp(jnic.second.as_s());
        if (!nic) {
            errh->error("Unknown NIC : %s",jnic.second.as_s().c_str());
            delete sc;
            return 0;
        }
        sc->nic.push_back(nic);
    }
    sc->_cpus.resize(sc->cpu_nr);
    if (!m->assignCpus(sc,sc->_cpus)) {
        errh->error("Could not assign enough CPUs");
        delete sc;
        return 0;
    }
    return sc;
}

Json Metron::ServiceChain::toJSON() {
    Json jsc = Json::make_object();
    jsc.set("id",getId());
    jsc.set("rxFilter",rxFilter->toJSON());
    jsc.set("config",config);
    jsc.set("expanded_config", generateConfig());
    jsc.set("cpus", getCpuNr());
    jsc.set("status", status);
    Json jnics = Json::make_array();
    for (auto n : nic) {
        jnics.push_back(Json::make_string(n->getId()));
    }
    jsc.set("nics",jnics);
    return jsc;
}

String Metron::ServiceChain::generateConfig()
{
    String newconf = "elementclass MetronSlave {\n" + config + "\n};\n\n";
    newconf += "slave :: MetronSlave();\n\n";
    for (int i = 0; i < nic.size(); i++) {
       String is = String(i);
       for (int j = 0; j < cpu_nr; j++) {
           String js = String(j);
           int cpuid = _cpus[j];
           int queue_no = rxFilter->cpuToQueue(nic[i],cpuid);
           newconf += "slaveFD"+is+ "C"+js+" :: "+nic[i]->element->class_name()+"("+nic[i]->getDeviceId()+",QUEUE "+String(queue_no)+", N_QUEUES 1,THREADOFFSET " +String(cpuid)+ ", MAXTHREADS 1, VERBOSE 99);\n";
           newconf += "slaveFD"+is+ "C"+js+" -> [" + is + "]slave;\n";
       }

       newconf += "slaveTD"+is+ " :: ToDPDKDevice("+nic[i]->getDeviceId()+");"; //TODO : allowed CPU bitmap
       newconf += "slave[" + is + "] -> slaveTD" + is + ";\n";
    }
    return newconf;
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)

EXPORT_ELEMENT(Metron)
ELEMENT_MT_SAFE(Metron)
