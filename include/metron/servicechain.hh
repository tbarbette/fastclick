// -*- mode: c++; c-basic-offset: 4 -*-

#ifndef CLICK_METRON_SC_HH
#define CLICK_METRON_SC_HH

#include <click/vector.hh>

#include "elements/json/json.hh"
#include "elements/userlevel/metron.hh"

class ServiceChainManager {
    public:
        ServiceChainManager(ServiceChain *sc);
        virtual ~ServiceChainManager();

        virtual void kill_service_chain() = 0;
        virtual int run_service_chain(ErrorHandler *errh) = 0;

        virtual void run_load_timer() {};
        virtual void check_alive() {};

        virtual int activate_core(int new_cpu_id, ErrorHandler *errh) {return 0;};
        virtual int deactivate_core(int new_cpu_id, ErrorHandler *errh) {return 0;};

        virtual void do_autoscale(ErrorHandler *errh) {};

        virtual String fix_rule(NIC *nic, String rule);

        /**
         * Send a command to the thing managed. The operator obiously has to know the
         * underlying implementation
         */
        virtual String command(String cmd) {
            return "";
        };

        virtual Json nic_stats_to_json() {
            return Json::make_array();
        };

    protected:
        ServiceChain *_sc;

        inline Metron* metron() {
            return _sc->_metron;
        }
};

class PidSCManager : public ServiceChainManager  {
    public:
        PidSCManager(ServiceChain *sc) : ServiceChainManager(sc) {};
        ~PidSCManager() {};
        void check_alive();

    protected:
        int _pid;
};

class ClickSCManager : public PidSCManager {
    public:
        ClickSCManager(ServiceChain *sc, bool _add_extra) : PidSCManager(sc), add_extra(_add_extra) {};
        ~ClickSCManager() {};

        void kill_service_chain();
        void run_load_timer();
        int activate_core(int new_cpu_id, ErrorHandler *errh);
        int deactivate_core(int new_cpu_id, ErrorHandler *errh);
        int run_service_chain(ErrorHandler *errh);

        virtual String command(String cmd);

        virtual void do_autoscale(ErrorHandler *errh);
        bool add_extra;

    protected:
        Vector<String> build_cmd_line(int socketfd);

        void control_init(int fd, int pid);

        int control_read_line(String &line);

        void control_write_line(String cmd);

        String control_send_command(String cmd);

        int call(
            String fnt, bool has_response, String handler,
            String &response, String params
        );
        String simple_call_read(String handler);
        String simple_call_write(String handler);
        int call_read(String handler, String &response, String params = "");
        int call_write(String handler, String &response, String params = "");

        Json nic_stats_to_json();

        int _socket;
};

class StandaloneSCManager : public PidSCManager {
    public:
        StandaloneSCManager(ServiceChain *sc);
        ~StandaloneSCManager() {};

        void kill_service_chain();

        void run_load_timer();
        int run_service_chain(ErrorHandler *errh);

        virtual String fix_rule(NIC *nic, String rule);

    private:
        struct CPUStat {
            unsigned long long last_total;
            unsigned long long last_idle;
        };

        Vector<float> update_load(Vector<CPUStat> &v);

        int _sriov;
        Vector<CPUStat> _cpu_stats;
    };

#endif // CLICK_METRON_SC_HH
