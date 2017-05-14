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
    static int param_handler(int operation, String &param, Element *e, const Handler *, ErrorHandler *errh) CLICK_COLD;
    static String read_handler(Element *e, void *user_data) CLICK_COLD;
    static int write_handler(const String &data, Element *e, void *user_data, ErrorHandler* errh) CLICK_COLD;

    Json toJSON();
    Json statsToJSON();

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

        enum ScStatus{SC_OK,SC_FAILED};
        String id;
        RxFilter* rxFilter;
        String config;
        int cpu_nr;
        Vector<Metron::NIC*> nic;
        enum ScStatus status;

        ServiceChain(Metron* m) : _metron(m) {

        }

        ~ServiceChain() {
            for (auto n : nic) {
                delete n;
            }
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

        Bitvector assignedCpus();

        String generateConfig();

        Vector<String> buildCmdLine(int socketfd) {
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

        void controlInit(int fd, int pid) {
            _socket = fd;
            _pid = pid;
        }

        int controlReadLine(String& line) {
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

        void controlWriteLine(String cmd) {
            int n = write(_socket,(cmd + "\r\n").c_str(),cmd.length() + 1);
        }

        String controlSendCommand(String cmd) {
            controlWriteLine(cmd);
            String ret;
            controlReadLine(ret);
            return ret;
        }

        String callRead(String handler) {
            String ret = controlSendCommand("READ " + handler);
            click_chatter("GOT %s",ret.c_str());
            int code = atoi(ret.substring(0, 3).c_str());
            click_chatter("Code %d",code);
            ret = ret.substring(ret.find_left("\r\n") + 2);
            click_chatter("Data %s",ret.c_str());
            assert(ret.starts_with("DATA "));
            ret = ret.substring(5);
            int eof = ret.find_left("\r\n");
            int n = atoi(ret.substring(0, eof).c_str());
            click_chatter("Length %d",n);
            ret = ret.substring(2, n);
            return ret;
        }

    private:
        Metron* _metron;
        Vector<int> _cpus;
        int _socket;
        int _pid;
    };
    enum {
        h_resources,h_stats,
        h_chains, h_delete_chains,h_chains_stats
    };

    ServiceChain* findChainById(String id);
    int addChain(ServiceChain* sc, ErrorHandler *errh);
    int removeChain(ServiceChain* sc, ErrorHandler *errh);

    int getCpuNr() {
        return click_max_cpu_ids();
    }

    int getAssignedCpuNr();

    bool assignCpus(ServiceChain* sc, Vector<int>& map);

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
};

CLICK_ENDDECLS
#endif
