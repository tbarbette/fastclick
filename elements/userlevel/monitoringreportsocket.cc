// -*- mode: c++; c-basic-offset: 2 -*-
/*
 * monitoringreportsocket.{cc,hh} -- periodically reports timestamped
 * values of element handlers via a UDP or TCP socket.
 * Useful for element handlers that contain arithmetic values.
 * Can be used for monitoring purposes; e.g., to report the run-time
 * latency or throughput counters of various elements.
 *
 * Georgios Katsikas <georgios.katsikas@ri.se>
 *
 * Copyright (c) 2018 RISE SICS
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
#include <click/error.hh>
#include <click/args.hh>
#include <click/glue.hh>
#include <click/router.hh>

#include <chrono>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>

#include "monitoringreportsocket.hh"

CLICK_DECLS

// Default frequency is 2 seconds
static const unsigned DEF_FREQ = 2;
// Maximum frequency is 1 seconds
static const unsigned MAX_FREQ = 1;
// Each message starts with a timestamp of this length
static const unsigned TIMESTAMP_LEN = sizeof(int64_t);
// Each message ends with the name of the handler
static const unsigned HANDLER_LEN = 25;
// Messages cannot be smaller than this length
static const unsigned MIN_TOT_MSG_LEN = TIMESTAMP_LEN + HANDLER_LEN + sizeof(int64_t);

static String
canonical_handler_name(const String &n)
{
    const char *dot = find(n, '.');

    if (dot == n.begin() || (dot == n.begin() + 1 && n.front() == '0'))
        return n.substring(dot + 1, n.end());

    return n;
}

static inline void
int64_to_char(char buffer[], const int64_t number) {
    *(int64_t *)buffer = number;
}

static inline int64_t
char_to_int64(char buffer[]){
    int64_t number = 0;
    memcpy(&number, buffer, sizeof(int64_t));
    return number;
}

MonitoringReportSocket::MonitoringReportSocket()
    : _timer(this),
    _socktype_str("UDP"), _fd(-1), _active(-1),
    _family(AF_INET), _remote_port(0),
    _sndbuf(-1), _nodelay(1), _verbose(false),
    _tot_msg_length(MIN_TOT_MSG_LEN),
    _frequency(DEF_FREQ), _core_nb(0)
{
}

MonitoringReportSocket::~MonitoringReportSocket()
{
    _elementHandlers.clear();
}

int
MonitoringReportSocket::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String socktype;

    // First choose a socket type (TCP or UDP)
    Args args = Args(this, errh).bind(conf);
    if (args.read_mp("TYPE", socktype).execute() < 0)
        return -1;
    socktype = socktype.upper();

    if ((socktype != "TCP") && (socktype != "UDP")) {
        return errh->error("Unknown socket type `%s'. Use UDP or TCP", socktype.c_str());
    }
    _socktype_str = socktype;

    // Remove keyword arguments
    if (args
            .read("FREQUENCY", _frequency)
            .read("CORE_NB",   _core_nb)
            .read("SNDBUF",    _sndbuf)
            .read("NODELAY",   _nodelay)
            .read("VERBOSE",   _verbose)
            .consume() < 0)
        return -1;

    if (_frequency <= 0) {
        return errh->error(
            "Monitoring frequency is %d seconds, but must be greater or equal than %d seconds",
            _frequency, MAX_FREQ
        );
    }

    unsigned max_core_nb = click_max_cpu_ids();

    if ((_core_nb < 0) || (_core_nb >= max_core_nb)) {
        return errh->error(
            "Invalid CPU core number: %d. Only %d CPU cores are available.",
            _core_nb, max_core_nb
        );
    }

    _socktype = socktype == "TCP" ? SOCK_STREAM : SOCK_DGRAM;
    _protocol = socktype == "TCP" ? IPPROTO_TCP : IPPROTO_UDP;
    if (args.read_mp("ADDR", _remote_ip)
            .read_mp("PORT", IPPortArg(_protocol), _remote_port)
            .consume() < 0)
        return -1;

    // Start parsing the element handlers' list
    for (int i = 0; i < conf.size(); ++i) {
        String element_with_handler = canonical_handler_name(conf[i]);
        const char *dot = find(element_with_handler, '.');

        // There is no dot between the element name and handler
        if (dot == element_with_handler.end()) {
            return errh->error(
                "'%s' is not a valid element with a handler (element.handler format is expected)",
                element_with_handler.c_str()
            );
        }

        // Get the element's name (whatever there is before the dot)
        String element_name = element_with_handler.substring(element_with_handler.begin(), dot);
        Element *e = router()->find(element_name);

        if (!e) {
            return errh->error("No element '%s' in this Click configuration", element_name.c_str());
        }

        // Get the element's handler (whatever there is after the dot)
        String handler_name = element_with_handler.substring(dot + 1, element_with_handler.end());
        if (!handler_name || handler_name.empty()) {
            return errh->error("No handler for element '%s'", element_name.c_str());
        }

        const Handler *h = Router::handler(e, handler_name);

        if (!_elementHandlers[e].empty()) {
            _elementHandlers[e].push_back(handler_name);
        } else {
            Vector<String> handlers;
            handlers.push_back(handler_name);
            _elementHandlers[e] = handlers;
        }
    }

    if (_elementHandlers.empty()) {
        return errh->error("Please provide at least one element handler");
    }

    return 0;
}

int
MonitoringReportSocket::initialize_handler_error(ErrorHandler *errh, const char *message)
{
    return errh->error("%s", message);
}

int
MonitoringReportSocket::initialize_socket_error(ErrorHandler *errh, const char *syscall)
{
    // Preserve errno
    int e = errno;

    if (_fd >= 0) {
        close(_fd);
        _fd = -1;
    }

    return errh->error("%s: %s", syscall, strerror(e));
}

String
MonitoringReportSocket::check_handlers(ErrorHandler *errh)
{
    String status = "";

    // Go through the input elements and their handlers
    auto el = _elementHandlers.begin();
    while (el != _elementHandlers.end()) {
        Element *e = el.key();

        for (String handler_name : _elementHandlers[e]) {
            // Ask for a handler instance with this name
            const Handler *h = Router::handler(e, handler_name);

            // No such an element handler
            if (!h || !h->read_visible()) {
                status = "Element '" + e->name() + "' does not have a handler '" + handler_name + "'";
                return status;
            }
        }

        el++;
    }

    return status;
}

int
MonitoringReportSocket::initialize(ErrorHandler *errh)
{
    assert(_tot_msg_length >= MIN_TOT_MSG_LEN);

    // Before creating the socket, verify user's input
    String status = check_handlers(errh);
    if (!status.empty()) {
        return initialize_handler_error(errh, status.c_str());
    }

    // Retrieve server information
    _server = gethostbyname(_remote_ip.s().c_str());
    if (!_server) {
        return initialize_socket_error(errh, "gethostbyname");
    }

    // Open socket
    _fd = socket(_family, _socktype, _protocol);
    if (_fd < 0) {
        return initialize_socket_error(errh, "socket");
    }

    // Configure socket
    bzero((char *) &_remote, sizeof(_remote));
    _remote.sin_family = _family;
    bcopy(
        (char *)_server->h_addr,
        (char *)&_remote.sin_addr.s_addr,
        _server->h_length
    );
    _remote.sin_port = htons(_remote_port);
    _remote_len = sizeof(_remote);

#ifdef TCP_NODELAY
    // Disable Nagle algorithm
    if (_protocol == IPPROTO_TCP && _nodelay)
        if (setsockopt(_fd, IP_PROTO_TCP, TCP_NODELAY, &_nodelay, sizeof(_nodelay)) < 0)
            return initialize_socket_error(errh, "setsockopt(TCP_NODELAY)");
#endif

    // Set socket send buffer size
    if (_sndbuf >= 0)
        if (setsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &_sndbuf, sizeof(_sndbuf)) < 0)
            return initialize_socket_error(errh, "setsockopt(SO_SNDBUF)");

    // Connect
    if (_socktype == SOCK_STREAM) {
        if (connect(_fd, (struct sockaddr *)&_remote, _remote_len) < 0)
            return initialize_socket_error(errh, "connect");
        if (_verbose) {
            click_chatter(
                "%s: opened connection %d to %s:%d",
                declaration().c_str(), _fd,
                IPAddress(_remote.sin_addr).unparse().c_str(),
                ntohs(_remote.sin_port)
            );
        }
    }
    _active = _fd;

    assert(_active >= 0);
    assert(_remote_len > 0);

    // Nonblocking I/O and close-on-exec for the socket
    fcntl(_fd, F_SETFL, O_NONBLOCK);
    fcntl(_fd, F_SETFD, FD_CLOEXEC);

    click_chatter(
        "Connected to %s monitoring socket on %s:%d",
        _socktype_str.c_str(), _remote_ip.s().c_str(), _remote_port
    );

    // Schedule this element
    _timer.initialize(this);
    _timer.move_thread(_core_nb);
    _timer.schedule_after_sec(1);

    return 0;
}

void
MonitoringReportSocket::cleanup(CleanupStage)
{
    if ((_active >= 0) && (_active != _fd)) {
        close(_active);
        _active = -1;
    }

    if (_fd >= 0) {
        // Shut down the listening socket in case we forked
    #ifdef SHUT_RDWR
        shutdown(_fd, SHUT_RDWR);
    #else
        shutdown(_fd, 2);
    #endif
        close(_fd);
        _fd = -1;
    }
}

void
MonitoringReportSocket::close_active(void)
{
    if (_active >= 0) {
        close(_active);
        if (_verbose) {
            click_chatter(
                "%s: closed %s connection %d",
                declaration().c_str(), _socktype_str.c_str(), _active
            );
        }
        _active = -1;
    }
}

void
MonitoringReportSocket::run_timer(Timer *t)
{
    assert(_active >= 0);

    // Go through the input elements and their handlers
    auto el = _elementHandlers.begin();
    while (el != _elementHandlers.end()) {
        Element *e = el.key();

        // Create a timestamp to associate with the values of this handler
        std::chrono::milliseconds ms;
        ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        );
        const int64_t ts = reinterpret_cast<int64_t>(ms.count());

        // Create a fixed-size buffer and, at first, store a timestamp
        char buffer[_tot_msg_length];
        bzero(buffer, _tot_msg_length);
        int64_to_char(buffer, htonll(ts));

        for (String handler_name : _elementHandlers[e]) {
            // Ask for a handler instance with this name
            const Handler *h = Router::handler(e, handler_name);
            assert(h);

            // Read the value of this handler
            const String value_str = h->call_read(e, ErrorHandler::default_handler());
            const int64_t value = (int64_t) atoll(value_str.c_str());

            /**
             * Fill the second part of the buffer with the value
             * and name of this handler.
             * If handler's name is longer than HANDLER_LEN,
             * we send only the first HANDLER_LEN characters.
             */
            int64_to_char(&buffer[sizeof(ts)], htonll(value));
            snprintf(&buffer[sizeof(ts) + sizeof(value)], HANDLER_LEN, "%s", handler_name.c_str());
            buffer[sizeof(ts) + sizeof(value) + handler_name.length() + 1] = '\0';

            // Print the contents of the buffer we are about to send
            print_data(buffer, sizeof(buffer));

            // Attempt to send in potentially multiple tries
            bool status = false;
            do {
                status = write_data(buffer, sizeof(buffer));
                if (!status) {
                    click_chatter(
                        "Failed to report handler %s.%s",
                        e->name().c_str(), h->name().c_str()
                    );
                }
            } while (buffer && !status);
            

            if (_verbose) {            
                click_chatter(
                    "Handler %s.%s: Timestamp %" PRId64 " - Value: %" PRId64
                    " - Name %s - Message length %d bytes",
                    e->name().c_str(), h->name().c_str(),
                    ts, value, handler_name.c_str(), sizeof(buffer)
                );
            }
        }

        // Next element
        el++;
    }

    // Periodic scheduling of this element
    _timer.reschedule_after_sec(_frequency);
}

bool
MonitoringReportSocket::write_data(char *buffer, const int buffer_len)
{
    int len  = 0;
    int sent = 0;

    while (sent < buffer_len) {
        // Write segment
        if (_socktype == SOCK_STREAM) {
            len = write(_active, buffer, buffer_len);
        } else {
            len = sendto(
                _active, buffer, buffer_len,
                0, (struct sockaddr *) &_remote, _remote_len
            );
        }

        // Error
        if (len < 0) {
            // Out of memory or would block
            if (errno == ENOBUFS || errno == EAGAIN)
                return false;
            // Interrupted by signal, try again immediately
            else if (errno == EINTR)
                continue;
            // Connection probably terminated or other fatal error
            else {
                if (_verbose)
                    click_chatter("%s: %s", declaration().c_str(), strerror(errno));
                close_active();
                break;
            }
        } else {
            // This segment OK
            buffer += len;
            sent   += len;
            if (_verbose) {
                click_chatter("Sent %d bytes", len);
            }
        }
    }

    return true;
}

void
MonitoringReportSocket::print_data(char *buffer, const int buffer_len)
{
    int64_t ts;
    int64_t value;
    char *handler_name;

    memcpy(&ts,    (int64_t *)&buffer[0],          sizeof(ts));
    memcpy(&value, (int64_t *)&buffer[sizeof(ts)], sizeof(value));
    handler_name = &buffer[sizeof(ts) + sizeof(value)];

    click_chatter(
        "[Monitoring element %s] [Thread %d] Timestamp: %" PRId64 " - Value: %" PRId64 " - Name: %s",
        this->name().c_str(), click_current_cpu_id(), htonll(ts), htonll(value), handler_name
    );
}

void
MonitoringReportSocket::add_handlers()
{
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(MonitoringReportSocket)
