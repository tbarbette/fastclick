// -*- mode: c++; c-basic-offset: 2 -*-

#ifndef CLICK_MON_REPORT_SOCKET_HH
#define CLICK_MON_REPORT_SOCKET_HH

#include <click/element.hh>
#include <click/string.hh>
#include <click/task.hh>
#include <click/vector.hh>
#include <click/hashtable.hh>

CLICK_DECLS

/*
=c

MonitoringReportSocket("TCP", IP, PORTNUMBER [, I<KEYWORDS>])
MonitoringReportSocket("UDP", IP, PORTNUMBER [, I<KEYWORDS>])

=s comm

a socket transport (user-level)

=d

Transports timestamped values of elements' handlers via UDP or TCP sockets.
This element is meant for monitoring purposes, thus it is useful for handlers
that report numbers (e.g., latency or throughput counters etc.).
Acts as a client socket.
The actual structure of each message is <timestamp, handler value, handler name>.
A timestamp is a 64bit integer (typically 8 bytes).
A handler's value is a 64bit integer (typically 8 bytes).
We reserve a static number of characters (25) for the handler's name. If this name
is longer, then only 25 characters will be reported.

Keyword arguments are:

=over 4

=item FREQUENCY

Unsigned Integer. The frequency in seconds to schedule this element,
thus send data to the remote server.

=item CORE_NB

Unsigned Integer. CPU core ID to schedule this element.

=item SNDBUF

Unsigned integer. Sets the maximum size in bytes of the underlying
socket send buffer. The default value is set by the wmem_default
sysctl and the maximum allowed value is set by the wmem_max sysctl.

=item NODELAY

Boolean. Applies to TCP sockets only. If set, disable the Nagle
algorithm. This means that segments are always sent as soon as
possible, even if there is only a small amount of data. When not set,
data is buffered until there is a sufficient amount to send out,
thereby avoiding the frequent sending of small packets, which results
in poor utilization of the network. Default is true.

=item VERBOSE

Boolean. When true, MonitoringReportSocket will print additional messages.

=back

=e

  // A TCP client socket
  MonitoringReportSocket(TCP, 1.2.3.4, 80);

  // A UDP client socket
  MonitoringReportSocket(UDP, 1.2.3.4, 7000);

  // A UDP client socket. Data is sent every 5 seconds
  MonitoringReportSocket(UDP, 1.2.3.4, 7000, FREQUENCY 5);

=a ControlSocket, Socket */

class MonitoringReportSocket : public Element {
  public:

    MonitoringReportSocket() CLICK_COLD;
    ~MonitoringReportSocket() CLICK_COLD;

    const char *class_name() const override	{ return "MonitoringReportSocket"; }
    const char *port_count() const override  { return PORTS_0_0; }
    int configure_phase() const override { return CONFIGURE_PHASE_DEFAULT; }

    int configure(Vector<String> &conf, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    String check_handlers(ErrorHandler *);
    void add_handlers() CLICK_COLD;

    void run_timer(Timer *t) override;
    bool write_data(char *buffer, const int buffer_len);
    void print_data(char *buffer, const int buffer_len);

    void close_active(void);

protected:
    Timer _timer;

private:
    String _socktype_str;     // TCP or UDP
    int    _fd;               // Socket descriptor
    int    _active;           // Connection descriptor

    // Remote connection info
    struct hostent    *_server;
    struct sockaddr_in _remote;
    socklen_t          _remote_len;

    int _family;              // Socket family is AF_INET
    int _socktype;            // SOCK_STREAM or SOCK_DGRAM
    int _protocol;            // IPPROTO_TCP or IPPROTO_UDP
    IPAddress _remote_ip;     // address to connect() to or sendto()
    uint16_t  _remote_port;   // port to connect() to or sendto()

    int      _sndbuf;         // Maximum socket send buffer in bytes
    int      _nodelay;        // Disable Nagle algorithm
    bool     _verbose;        // Be verbose

    unsigned _frequency;      // Frequency at which data is sent through the socket
    unsigned _tot_msg_length; // Total number of bytes for a message
    unsigned _core_nb;        // CPU core to run this element

    HashTable<Element *, Vector<String>> _elementHandlers; // Elements to lists of handlers

    int initialize_handler_error(ErrorHandler *, const char *);
    int initialize_socket_error (ErrorHandler *, const char *);

};

CLICK_ENDDECLS
#endif
