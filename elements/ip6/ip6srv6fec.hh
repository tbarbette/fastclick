#ifndef CLICK_IP6SRv6FECEncode_HH
#define CLICK_IP6SRv6FECEncode_HH
#include <click/element.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#include <clicknet/ip6.h>
#include <click/ip6address.hh>

#define TLV_TYPE_FEC_SOURCE 28
#define TLV_TYPE_FEC_REPAIR 29
#define LOCAL_MTU 1500

CLICK_DECLS

/*
=c
IP6Encap(ENC, DEC)
=s ip
Executes Forward Erasure Correction (FEC) using IPv6 Segment Routing
=d
Protects each incoming packet with Forward Erasure Correction.
Adds a TLV field in the Segment Routing Header of each source symbol.
Creates new packets for the repair symbols with source=ENC and destination=DEC

Keyword arguments are:
TODO */

#ifndef TINYMT32_T_HH
#define TINYMT32_T_HH
/**
 * tinymt32 internal state vector and parameters
 */
struct TINYMT32_T {
    uint32_t status[4];
    uint32_t mat1;
    uint32_t mat2;
    uint32_t tmat;
};

typedef struct TINYMT32_T tinymt32_t;
#endif

// SRv6-FEC TLV structures
struct source_tlv_t {
  uint8_t type;
  uint8_t len;
  uint16_t padding;
  uint32_t sfpid; // Source FEC Payload ID
}; // TODO: make them all packet

struct repair_tlv_t {
  uint8_t type;
  uint8_t len;
  uint16_t padding;
  uint32_t rfpid; // Repair FEC Payload ID
  uint32_t rfi; // Repair FEC Info
  uint16_t coded_length;
  uint8_t nss; // Number Source Symbol
  uint8_t nrs; // Number Repair Symbol
};

struct rlc_info_t {
  uint32_t encoding_symbol_id;
  uint16_t repair_key;
  uint8_t window_size;
  uint8_t window_step;
  uint8_t buffer_size;

  // RLC relative information
  tinymt32_t prng;
  uint16_t max_length; // Seen for this window
  uint8_t muls[256 * 256 * sizeof(uint8_t)];
};

class IP6SRv6FECEncode : public Element { 
 
 public:

  IP6SRv6FECEncode();
  ~IP6SRv6FECEncode();

  const char *class_name() const override        { return "IP6SRv6FECEncode"; }
  const char *port_count() const override        { return PORTS_1_1; }

  int  configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  bool can_live_reconfigure() const     { return true; }
  void add_handlers() CLICK_COLD;

  void push(int, Packet *) override;

 private:

  source_tlv_t _source_tlv;
  repair_tlv_t _repair_tlv;
  rlc_info_t   _rlc_info;
  WritablePacket *_repair_packet; // Or public ?
  bool _use_dst_anno;
  IP6Address enc; // Encoder SID
  IP6Address dec; // Decoder SID

  static String read_handler(Element *, void *) CLICK_COLD;
  void fec_framework(Packet *p_in) CLICK_COLD;
  int fec_scheme(Packet *p_in) CLICK_COLD;
  void rlc_encode_otl(Packet *p) CLICK_COLD;
  
  uint8_t rlc_get_coef() CLICK_COLD;
  void rlc_reset_coefs() CLICK_COLD;
  void rlc_init_muls() CLICK_COLD;

};

CLICK_ENDDECLS
#endif
