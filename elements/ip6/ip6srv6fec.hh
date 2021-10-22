#ifndef CLICK_IP6SRv6FECEncode_HH
#define CLICK_IP6SRv6FECEncode_HH
#include <click/element.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#include <clicknet/ip6.h>
#include <click/ip6address.hh>
#include <tinymt32/tinymt32.h>
#include <swifsymbol/swifsymbol.h>

#ifndef SRV6FEC_HH
#define SRV6FEC_HH
#define SRV6_FEC_BUFFER_SIZE 32

#define TLV_TYPE_FEC_SOURCE 28
#define TLV_TYPE_FEC_REPAIR 29
#define RLC_MAX_WINDOWS 4
#define LOCAL_MTU 1500

// SRv6-FEC TLV structures
struct source_tlv_t {
  uint8_t type;
  uint8_t len;
  uint16_t padding;
  uint32_t sfpid; // Source FEC Payload ID
} CLICK_SIZE_PACKED_ATTRIBUTE;

struct repair_tlv_t {
  uint8_t type;
  uint8_t len;
  uint16_t padding;
  uint32_t rfpid; // Repair FEC Payload ID
  union {
    struct {
      uint8_t previous_window_size;
      uint8_t window_step;
      uint16_t repair_key;
    } rlc_rfi;
    uint32_t rfi;
  }; // Repair FEC Info
  uint16_t coded_length;
  uint8_t nss; // Number Source Symbol
  uint8_t nrs; // Number Repair Symbol
} CLICK_SIZE_PACKED_ATTRIBUTE;

#endif

CLICK_DECLS

/*
=c

IP6SRv6FECEncode(ENC, DEC)

=s ip

Forward Erasure Correction for IPv6 Segment Routing

=d

Takes the encoder and decoder SIDs

=e


  IP6SRv6FECEncode(fc00::a, fc00::9)

=a IP6Encap */

struct rlc_info_t {
  uint32_t encoding_symbol_id;
  uint16_t repair_key;
  uint8_t window_size;
  uint8_t window_step;
  uint8_t buffer_size;
  uint8_t previous_window_step;

  // RLC relative information
  tinymt32_t prng;
  uint16_t max_length; // Seen for this window
  uint8_t muls[256 * 256 * sizeof(uint8_t)];
  Packet *source_buffer[SRV6_FEC_BUFFER_SIZE]; // Ring buffer
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

  void push(int, Packet * p_in) override;

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
  void store_source_symbol(Packet *p_in, uint32_t encodind_symbol_id) CLICK_COLD;
  void rlc_encode_symbols(uint32_t encoding_symbol_id) CLICK_COLD;
  void rlc_free_out_of_window(uint32_t encoding_symbol_id) CLICK_COLD;
  void encapsulate_repair_payload(WritablePacket *p, repair_tlv_t *tlv, IP6Address *encoder, IP6Address *decoder, uint16_t packet_length) CLICK_COLD;
  WritablePacket *srv6_fec_add_source_tlv(Packet *p_in, source_tlv_t *tlv) CLICK_COLD;
  tinymt32_t rlc_reset_coefs() CLICK_COLD;
  void rlc_fill_muls(uint8_t muls[256 * 256]) CLICK_COLD;
  uint8_t rlc_get_coef(tinymt32_t *prng) CLICK_COLD;
  void rlc_encode_one_symbol(Packet *s, WritablePacket *r, tinymt32_t *prng, uint8_t muls[256 * 256 * sizeof(uint8_t)], repair_tlv_t *repair_tlv) CLICK_COLD;
};



CLICK_ENDDECLS
#endif
