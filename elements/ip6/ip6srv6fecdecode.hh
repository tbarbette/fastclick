#ifndef CLICK_IP6SRv6FECDecode_HH
#define CLICK_IP6SRv6FECDecode_HH
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
#define DECODER_MAX_WINDOWS 4
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
  uint32_t rfi; // Repair FEC Info
  uint16_t coded_length;
  uint8_t nss; // Number Source Symbol
  uint8_t nrs; // Number Repair Symbol
} CLICK_SIZE_PACKED_ATTRIBUTE;
#endif

CLICK_DECLS

/*
=c

IP6SRv6FECDecode()

=s ip

Forward Erasure Correction for IPv6 Segment Routing

=d

=e


  IP6SRv6FECDecode(fc00::a, fc00::9)

=a IP6Encap */

struct srv6_fec2_source_t {
  Packet *p;
  source_tlv_t tlv;
};

struct srv6_fec2_repair_t {
  Packet *p;
  repair_tlv_t tlv;
};

struct rlc_info_decoder_t {

  // RLC relative information
  tinymt32_t prng;
  uint8_t muls[256 * 256 * sizeof(uint8_t)];
  srv6_fec2_source_t *source_buffer[SRV6_FEC_BUFFER_SIZE];
  srv6_fec2_repair_t *repair_buffer[SRV6_FEC_BUFFER_SIZE];
};

class IP6SRv6FECDecode : public Element { 
 
 public:

  IP6SRv6FECDecode();
  ~IP6SRv6FECDecode();

  const char *class_name() const override        { return "IP6SRv6FECDecode"; }
  const char *port_count() const override        { return PORTS_1_1; }

  int  configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  bool can_live_reconfigure() const     { return true; }
  void add_handlers() CLICK_COLD;

  void push(int, Packet *p_in) override;

 private:

  bool _use_dst_anno;
  rlc_info_decoder_t _rlc_info;

  static String read_handler(Element *, void *) CLICK_COLD;
  void fec_framework(Packet *p_in) CLICK_COLD;
  int fec_scheme_source(Packet *p_in, source_tlv_t *tlv) CLICK_COLD;
  int fec_scheme_repair(Packet *p_in, repair_tlv_t *tlv) CLICK_COLD;
  
  tinymt32_t rlc_reset_coefs() CLICK_COLD;
  void rlc_fill_muls(uint8_t muls[256 * 256]) CLICK_COLD;
  uint8_t rlc_get_coef(tinymt32_t *prng) CLICK_COLD;

  void store_source_symbol(Packet *p_in, source_tlv_t *tlv) CLICK_COLD;
  void store_repair_symbol(Packet *p_in, repair_tlv_t *tlv) CLICK_COLD;
  void remove_tlv_source_symbol(WritablePacket *p, uint16_t offset_tlv) CLICK_COLD;

};

CLICK_ENDDECLS
#endif

