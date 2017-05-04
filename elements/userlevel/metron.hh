// -*- c-basic-offset: 4 -*-
#ifndef CLICK_METRON_HH
#define CLICK_METRON_HH
#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/task.hh>
#include <click/notifier.hh>
#include <click/hashmap.hh>

#include "../json/json.hh"
CLICK_DECLS

/*
=c

Metron */

class Metron : public Element { public:

	Metron() CLICK_COLD;
    ~Metron() CLICK_COLD;

    const char *class_name() const	{ return "Metron"; }
    const char *port_count() const	{ return PORTS_0_0; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;
    void add_handlers() CLICK_COLD;
    static String read_handler(Element *e, void *user_data) CLICK_COLD;
    static int write_handler(const String &data, Element *e, void *user_data, ErrorHandler* errh) CLICK_COLD;


    class NIC { public:
        Element* element;

        String getId() {
            return element->name();
        }

        Json toJSON();
    };

    class ServiceChain { public:
        enum ScStatus{SC_OK,SC_FAILED};
        String id;
        int vlanid;
        String config;
        int cpu;
        Vector<Metron::NIC*> nic;
        enum ScStatus status;

        static ServiceChain* fromJSON(Json j,Metron* m, ErrorHandler* errh);

        Json toJSON();

        String getId() {
            return id;
        }

        String generateConfig() {
            return config;
        }
    };
    enum {
        h_resources,
	h_chains
    };

    int addChain(ServiceChain* sc, ErrorHandler *errh);

private:

    HashMap<String,NIC> _nics;
    HashMap<String,ServiceChain*> _scs;

};

CLICK_ENDDECLS
#endif
