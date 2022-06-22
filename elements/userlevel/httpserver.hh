// -*- c-basic-offset: 4 -*-
#ifndef CLICK_HTTPSERVER_HH
#define CLICK_HTTPSERVER_HH
#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/task.hh>
#include <click/notifier.hh>
#include <click/multithread.hh>
#include <click/hashmap.hh>
#include <click/ring.hh>
#include <microhttpd.h>

CLICK_DECLS

#if MHD_VERSION < 0x00097500
#define MHD_Result int
#endif


/*
=c

HTTPServer */

class HTTPServer : public Element { public:

    HTTPServer() CLICK_COLD;
    ~HTTPServer() CLICK_COLD;

    const char *class_name() const override  { return "HTTPServer"; }
    const char *port_count() const override  { return PORTS_0_0; }


    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    void selected(int fd, int mask);
    void update_fd_set();

    static MHD_Result ahc_echo(
        void *cls,
        struct MHD_Connection *connection,
        const char *url,
        const char *method,
        const char *version,
        const char *upload_data,
        size_t *upload_data_size,
        void **ptr
    );
private:
    int _port;
    HashMap<String, String> _alias_map;
    bool _verbose;
    struct MHD_Daemon * _daemon;


    class Request {
    public:
        struct MHD_Connection * connection;

        Request() : connection(0) {

        }

    };
    MPMCRing<Request*,32> _requests;

};

CLICK_ENDDECLS
#endif
