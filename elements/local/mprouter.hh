#pragma once
/*
=c

MultipathRouter(PREFIX1 OUT1, PREFIX2 OUT2, ...)

=s iproute

A round-robin multipath router.

=d

MultipathRouter presents the click IP router interface round in elements such
as LinearIPLookup. MultipathRouter can handle multiple paths for the same
route. It handles multipath conditions in a round-robin fashion. The router
supports 32,24,16,8 and 0 length prefix routes. The longest prefix is always
chosen in the case of multiple viable routes to choose from.

=e

  MultipathRouter(
    10.0.0.0/24 0,
    10.0.0.0/24 1,
    10.0.1.0/24 2,
    10.0.1.0/24 3
  );

=n

Route lookups are using the C++ std::map.

=a LinearIPLookup
*/

#include <map>
#include <vector>
#include <click/config.h>
#include <click/element.hh>
#include "../ip/iproutetable.hh"

CLICK_DECLS

struct Path {
  std::vector<unsigned short> ports{};
  unsigned short robin{};
};

class MultipathRouter : public Element {

  public:

    // lifetime management
    MultipathRouter() CLICK_COLD = default;
    ~MultipathRouter() CLICK_COLD = default;
    int configure(Vector<String>&, ErrorHandler*) override final CLICK_COLD;

    // packet processing
    void push(int port, Packet *p) override final;
    void take_path(int port, Packet*, Path&);

    // metadata
    const char* class_name() const override final { return "MultipathRouter"; }
    const char* processing() const override final;
    const char* port_count() const override final;

  private:

    bool add_route(const IPRoute&, ErrorHandler*);
    void add_route_path(std::map<uint32_t,Path>&, uint32_t, uint32_t);
    void add_default_path(uint32_t);

    // prefix -> path
    // TODO currently only ipv4
    std::map<uint32_t,Path> _t32{}, // /32 table
                            _t24{}, // /24 table
                            _t16{}, // /16 table
                            _t8{};  // /8 table

    Path *_default{nullptr}; // default path (/0)

    uint32_t _p24 = IPAddress::make_prefix(24).addr(),
             _p16 = IPAddress::make_prefix(16).addr(),
             _p8 =  IPAddress::make_prefix(8).addr();


};
