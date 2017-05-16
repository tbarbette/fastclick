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

class Metron;

class NIC { public:
    Element* element;

    String getId() {
        return element->name();
    }

    String getDeviceId();

    Json toJSON(bool stats = false);

    int queuePerPool() {
        return atoi(callRead("nb_rx_queues").c_str()) / atoi(callRead("nb_vf_pools").c_str());
    }

    int cpuToQueue(int id) {
        return id * (queuePerPool());
    }

    String callRead(String h);
    String callTxRead(String h);
};

class ServiceChain { public:
        class RxFilter { public:

            RxFilter(ServiceChain* sc) : _sc(sc) {

            }

            String method;
            Vector<String> addr;
            ServiceChain* _sc;

            static RxFilter* fromJSON(Json j, ServiceChain* sc, ErrorHandler* errh);
            Json toJSON();

            int cpuToQueue(NIC* nic, int cpuid) {
                return nic->cpuToQueue(cpuid);
            }

            virtual int apply(NIC* nic, Bitvector cpus, ErrorHandler* errh);
        };

        enum ScStatus{SC_FAILED,SC_OK=1};
        String id;
        RxFilter* rxFilter;
        String config;
        int cpu_nr;
        Vector<NIC*> nic;
        enum ScStatus status;

        ServiceChain(Metron* m) : rxFilter(0),_metron(m) {

        }

        ~ServiceChain() {
            //Do not delete nics, we are not the owner of those pointers
            if (rxFilter)
                delete rxFilter;
        }

        static ServiceChain* fromJSON(Json j,Metron* m, ErrorHandler* errh);

        Json toJSON();
        Json statsToJSON();

        inline String getId() {
            return id;
        }

        inline int getCpuNr() {
            return cpu_nr;
        }

        inline int getCpuMap(int i) {
            return _cpus[i];
        }


        Bitvector assignedCpus();

        String generateConfig();

        Vector<String> buildCmdLine(int socketfd);

        void controlInit(int fd, int pid);

        int controlReadLine(String& line);

        void controlWriteLine(String cmd);

        String controlSendCommand(String cmd);

        void checkAlive();

        String callRead(String handler);

        Vector<int>& getCpuMapRef() {
            return _cpus;
        }

        struct timing_stats {
            Timestamp start,parse,launch;
            Json toJSON();
        };
        void setTimingStats(struct timing_stats ts) {
            _timing_stats = ts;
        }

    private:
        Metron* _metron;
        Vector<int> _cpus;
        int _socket;
        int _pid;
        struct timing_stats _timing_stats;
    };

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
    static int param_handler(int operation, String &param, Element *e, const Handler *, ErrorHandler *errh) CLICK_COLD;
    static String read_handler(Element *e, void *user_data) CLICK_COLD;
    static int write_handler(const String &data, Element *e, void *user_data, ErrorHandler* errh) CLICK_COLD;

    Json toJSON();
    Json statsToJSON();

    enum {
        h_resources,h_stats,
        h_chains, h_delete_chains,h_chains_stats,h_chains_proxy
    };

    ServiceChain* findChainById(String id);

    int removeChain(ServiceChain* sc, ErrorHandler *errh);
    int instanciateChain(ServiceChain* sc, ErrorHandler *errh);

    int getCpuNr() {
        return click_max_cpu_ids();
    }

    int getNbChains() {
        return _scs.size();
    }

    int getAssignedCpuNr();

    bool assignCpus(ServiceChain* sc, Vector<int>& map);
    void unassignCpus(ServiceChain* sc);

private:
    String _id;
    Vector<String> _args;
    Vector<String> _dpdk_args;

    HashMap<String,NIC> _nics;
    HashMap<String,ServiceChain*> _scs;

    Vector<ServiceChain*> _cpu_map;

    String _vendor;
    String _hw;
    String _sw;
    String _serial;

    bool _timing_stats;

    int runChain(ServiceChain* sc, ErrorHandler *errh);

    friend class ServiceChain;
};

CLICK_ENDDECLS
#endif
