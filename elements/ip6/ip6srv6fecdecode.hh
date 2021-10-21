#ifndef CLICK_IP6SRv6FECDecode_HH
#define CLICK_IP6SRv6FECDecode_HH
#include <click/element.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#include <clicknet/ip6.h>
#include <click/ip6address.hh>
#include <tinymt32/tinymt32.h>
#include <swifsymbol/swifsymbol.h>

#define symbol_sub_scaled_term symbol_add_scaled_term

#ifndef SRV6FEC_HH
#define SRV6FEC_HH
#define SRV6_FEC_COPY_PACKET
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
  uint32_t rfi; // Repair FEC Info
  uint16_t coded_length;
  uint8_t nss; // Number Source Symbol
  uint8_t nrs; // Number Repair Symbol
} CLICK_SIZE_PACKED_ATTRIBUTE;

struct my_packet_t {
#ifdef SRV6_FEC_COPY_PACKET
  uint8_t *data;
  uint16_t packet_length;
#else
  WritablePacket *p;
#endif
};

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
  my_packet_t *p;
  source_tlv_t tlv;
};

struct srv6_fec2_repair_t {
  my_packet_t *p;
  repair_tlv_t tlv;
};

struct srv6_fec2_source_term_t {
  my_packet_t *p;
  uint32_t encoding_symbol_id;
};

struct rlc_info_decoder_t {

  // RLC relative information
  tinymt32_t prng;
  uint8_t muls[256 * 256 * sizeof(uint8_t)];
  uint8_t table_inv[256 * sizeof(uint8_t)];
  srv6_fec2_source_t *source_buffer[SRV6_FEC_BUFFER_SIZE];
  srv6_fec2_repair_t *repair_buffer[SRV6_FEC_BUFFER_SIZE];
  srv6_fec2_source_term_t *recovd_buffer[SRV6_FEC_BUFFER_SIZE];
  uint32_t encoding_symbol_id;
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

  IP6Address dec; // Decoder SID
  bool _use_dst_anno;
  rlc_info_decoder_t _rlc_info;

  static String read_handler(Element *, void *) CLICK_COLD;
  void fec_framework(Packet *p_in) CLICK_COLD;
  int fec_scheme_source(WritablePacket *p_in, source_tlv_t *tlv) CLICK_COLD;
  int fec_scheme_repair(WritablePacket *p_in, repair_tlv_t *tlv) CLICK_COLD;
  WritablePacket *recover_packet_fom_data(uint8_t *data, uint16_t packet_length);

  void rlc_fill_muls(uint8_t muls[256 * 256]) CLICK_COLD;
  my_packet_t *init_clone(Packet *p, uint16_t packet_length) CLICK_COLD;
  void kill_clone(my_packet_t *p) CLICK_COLD;

  void store_source_symbol(WritablePacket *p_in, source_tlv_t *tlv) CLICK_COLD;
  void store_repair_symbol(WritablePacket *p_in, repair_tlv_t *tlv) CLICK_COLD;
  void remove_tlv_source_symbol(WritablePacket *p, uint16_t offset_tlv) CLICK_COLD;

  void rlc_recover_symbols();
  void rlc_get_coefs(tinymt32_t *prng, uint32_t seed, int n, uint8_t *coefs);
  void symbol_add_scaled_term(my_packet_t *symbol1, uint8_t coef, srv6_fec2_source_term_t *symbol2, uint8_t *mul);
  void symbol_add_scaled_term(my_packet_t *symbol1, uint8_t coef, my_packet_t *symbol2, uint8_t *mul, uint16_t decoding_size);
  void symbol_mul_term(my_packet_t *symbol1, uint8_t coef, uint8_t *mul, uint16_t size);

  void swap(uint8_t **a, int i, int j);
  void swap_b(my_packet_t **a, int i, int j);
  int cmp_eq_i(uint8_t *a, uint8_t *b, int idx, int n_unknowns);
  int cmp_eq(uint8_t *a, uint8_t *b, int idx, int n_unknowns);
  void sort_system(uint8_t **a, my_packet_t **constant_terms, int n_eq, int n_unknowns);
  int first_non_zero_idx(const uint8_t *a, int n_unknowns);
  void gauss_elimination(int n_eq, int n_unknowns, uint8_t **a, my_packet_t **constant_terms, my_packet_t **x, bool *undetermined, uint8_t *mul, uint8_t *inv, uint16_t max_packet_length);
  void print_packet(my_packet_t *p);
  void print_packet(srv6_fec2_source_term_t *p);
};

CLICK_ENDDECLS
#endif

