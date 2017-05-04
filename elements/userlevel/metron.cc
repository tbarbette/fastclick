#include <click/config.h>

#include "metron.hh"

#include <click/args.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/userutils.hh>



CLICK_DECLS


Metron::Metron() {

}

Metron::~Metron() {

}


int Metron::configure(Vector<String> &conf, ErrorHandler *errh) {
    Vector<Element*> nics;
    if (Args(conf, this, errh)
        .read_all("NIC",nics)
        .complete() < 0)
        return -1;

    for (Element* e : nics) {
        NIC nic;
        nic.element = e;
        _nics.insert(nic.getId(), nic);
    }
    return 0;
}


int Metron::initialize(ErrorHandler *) {

	return 0;
}

void Metron::cleanup(CleanupStage) {
    auto begin = _scs.begin();
    while (begin != _scs.end()) {
        delete begin.value();
    }
}

String Metron::read_handler(Element *e, void *user_data) {
	Metron *m = static_cast<Metron *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    Json jroot = Json::make_object();

    switch (what) {
    	case h_resources: {
			//Cpu ressources
			jroot.set("cpu",Json(click_max_cpu_ids()));

			//Nics ressources
			Json jnics = Json::make_array();
			auto begin = m->_nics.begin();
			while (begin != m->_nics.end()) {
				jnics.push_back(begin.value().toJSON());
			    begin++;
			}
			jroot.set("nic",jnics);
			break;
    	}
        case h_chains: {
            //Nics ressources
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
    return jroot.unparse();
}

int Metron::addChain(ServiceChain* sc, ErrorHandler *errh) {
    _scs.insert(sc->getId(),sc);


    //Launch slave
    int pid = fork();
    if (pid == -1) {
        return errh->error("Fork error. Too many processes?");
    }
    if (pid == 0) {
        String cpulist = "";
        for (int i = 0; i < click_max_cpu_ids(); i++) {
            cpulist += String(i) + (i < click_max_cpu_ids() -1? ",":"");
        }

        char* program = click_path;

        char* argv[] = {
                program,
                "--dpdk",
                "-l",
                const_cast<char*>(cpulist.c_str()),
                "--proc-type=secondary",
                "--"
        };
        int ret;
        if (ret = execv(program, argv)) {
            click_chatter("Could not launch slave process : %d %d",ret,errno);
        }
        exit(1);
    } else {
        click_chatter("Child %d launched successfully",pid);
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
}

void Metron::add_handlers() {
	add_read_handler("resources", read_handler, h_resources);
	add_read_handler("chains", read_handler, h_chains);
	add_write_handler("chains", write_handler, h_chains);
}

/**
 * NIC
 */
Json Metron::NIC::toJSON() {
    Json nic = Json::make_object();
    nic.set("id",getId());
    const Handler* hS = Router::handler(element, "speed");
    if (hS && hS->visible()) {
        nic.set("speed",hS->call_read(element, ErrorHandler::default_handler()));
    }
    const Handler* hC = Router::handler(element, "carrier");
    if (hC && hC->visible()) {
        nic.set("status",hC->call_read(element, ErrorHandler::default_handler()));
    }
    return nic;
}

/**
 * Service Chain
 */
Metron::ServiceChain* Metron::ServiceChain::fromJSON(Json j, Metron* m, ErrorHandler* errh) {
    Metron::ServiceChain* sc = new ServiceChain();
    sc->id = j.get_s("id");
    sc->vlanid = j.get_i("vlanid");
    sc->config = j.get_s("config");
    sc->cpu = j.get_i("cpu");
    Json jnics = j.get("nic");
    for (auto jnic : jnics) {
        Metron::NIC* nic = m->_nics.findp(jnic.second.as_s());
        if (!nic) {
            errh->error("Unknown NIC : %s",jnic.second.as_s().c_str());
            return 0;
        }
        sc->nic.push_back(nic);
    }
    return sc;
}

Json Metron::ServiceChain::toJSON() {
    Json jsc = Json::make_object();
    jsc.set("id",getId());
    jsc.set("vlanid",vlanid);
    jsc.set("config",config);
    jsc.set("expanded_config",generateConfig());
    jsc.set("cpu",cpu);
    Json jnics = Json::make_array();
    for (auto n : nic) {
        jnics.push_back(Json::make_string(n->getId()));
    }
    jsc.set("nic",jnics);
    return jsc;
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)

EXPORT_ELEMENT(Metron)
ELEMENT_MT_SAFE(Metron)
