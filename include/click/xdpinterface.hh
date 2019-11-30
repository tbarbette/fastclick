#pragma once

#include <memory>
#include <map>
#include <click/xdp.hh>
#include <click/config.h>
#include <click/packet.hh>

// maps a queue index to a vector of packets
using XDPPacketMap = std::map<u32, vector<Packet*>>;

class XDPInterface : public std::enable_shared_from_this<XDPInterface> {

  public:
    XDPInterface(
        string dev,
        string prog,
        u16 xdp_flags,
        u16 bind_flags,
        bool trace=false
    );

    void init();
    XDPPacketMap rx();
    void tx(Packet *p, u32 queue_id);
    void kick(u32 queue_id);

    // accessors
    inline const vector<XDPSockSP> & socks() const  { return _socks; }
    inline const string & dev() const               { return _dev; }
    inline const string & prog() const              { return _prog; }
    inline int bpf_fd() const                       { return _bpf_fd; }
    inline int xsks_map_fd() const                  { return _xsks_map_fd; }
    inline u16 xdp_flags() const                    { return _xdp_flags; }
    inline u16 bind_flags() const                   { return _bind_flags; }
    inline uint ifindex() const                     { return _ifindex; }

  private:
    void create_device_sockets();
    void load_bpf();
    void load_bpf_program();
    void load_bpf_maps();

    std::string              _dev,
                             _prog;
    u16                      _xdp_flags,
                             _bind_flags;
    struct bpf_map           *_xsks_map;
    struct bpf_object        *_bpf_obj;
    int                      _bpf_fd,
                             _xsks_map_fd;
    uint                     _ifindex;
    std::vector<XDPSockSP>   _socks;
    std::vector<pollfd>      _poll_fds;
    bool                     _trace;
  
};

