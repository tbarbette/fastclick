#ifndef CLICK_IP6SRv6FECDecode_HH
#define CLICK_IP6SRv6FECDecode_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#include <clicknet/ip6.h>
#include <click/ip6address.hh>
#include <tinymt32/tinymt32.h>
#include <swifsymbol/swifsymbol.h>

#define symbol_sub_scaled_term symbol_add_scaled_term

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

IP6SRv6FECDecode()

=s ip

Forward Erasure Correction for IPv6 Segment Routing

=d

=e


  IP6SRv6FECDecode(fc00::a, fc00::9)

=a IP6Encap */

struct srv6_fec2_source_t {
  Packet *p;
  uint32_t encoding_symbol_id;
};

struct srv6_fec2_repair_t {
  Packet *p;
  repair_tlv_t tlv;
};

struct srv6_fec2_term_t {
  uint8_t *data;
  union {
    uint16_t coded_length;
    uint16_t data_length;
  } length;
};

struct rlc_info_decoder_t {

  // RLC relative information
  tinymt32_t prng;
  uint8_t muls[256 * 256 * sizeof(uint8_t)];
  uint8_t table_inv[256 * sizeof(uint8_t)];
  srv6_fec2_source_t *source_buffer[SRV6_FEC_BUFFER_SIZE];
  srv6_fec2_repair_t *repair_buffer[SRV6_FEC_BUFFER_SIZE];
  srv6_fec2_source_t *recovd_buffer[SRV6_FEC_BUFFER_SIZE];
  uint32_t encoding_symbol_id;
};

#define SRV6_FEC_RLC 0
#define SRV6_FEC_XOR 1

class IP6SRv6FECDecode : public BatchElement { 
 
 public:

  IP6SRv6FECDecode();
  ~IP6SRv6FECDecode();

  const char *class_name() const override        { return "IP6SRv6FECDecode"; }
  const char *port_count() const override        { return PORTS_1_1; }

  int  configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  bool can_live_reconfigure() const     { return true; }
  void add_handlers() CLICK_COLD;

  void push(int, Packet *p_in) override;
#if HAVE_BATCH
  void push_batch(int, PacketBatch * batch_in) override;
#endif

 private:

  IP6Address dec; // Decoder SID
  bool _use_dst_anno;
  rlc_info_decoder_t _rlc_info;

  static String read_handler(Element *, void *) CLICK_COLD;

  void fec_framework(Packet *p_in, std::function<void(Packet*)>push);
  int fec_scheme_source(WritablePacket *p_in, source_tlv_t *tlv);
  int fec_scheme_repair(WritablePacket *p_in, repair_tlv_t *tlv);
  WritablePacket *recover_packet_fom_data(srv6_fec2_term_t *rec);
  srv6_fec2_term_t *init_term(Packet *p, uint16_t offset, uint16_t max_packet_length);
  void kill_term(srv6_fec2_term_t *t);

  void rlc_fill_muls(uint8_t muls[256 * 256]);

  void store_source_symbol(WritablePacket *p_in, source_tlv_t *tlv);
  void store_repair_symbol(WritablePacket *p_in, repair_tlv_t *tlv);
  void remove_tlv_source_symbol(WritablePacket *p, uint16_t offset_tlv);

  Packet* rlc_recover_symbols();
  void rlc_get_coefs(tinymt32_t *prng, uint32_t seed, int n, uint8_t *coefs);
  void symbol_add_scaled_term(srv6_fec2_term_t *symbol1, uint8_t coef, srv6_fec2_source_t *symbol2, uint8_t *mul);
  void symbol_add_scaled_term(srv6_fec2_term_t *symbol1, uint8_t coef, srv6_fec2_term_t *symbol2, uint8_t *mul, uint16_t decoding_size);
  void symbol_mul_term(srv6_fec2_term_t *symbol1, uint8_t coef, uint8_t *mul, uint16_t size);

  void swap(uint8_t **a, int i, int j);
  void swap_b(srv6_fec2_term_t **a, int i, int j);
  int cmp_eq_i(uint8_t *a, uint8_t *b, int idx, int n_unknowns);
  int cmp_eq(uint8_t *a, uint8_t *b, int idx, int n_unknowns);
  void sort_system(uint8_t **a, srv6_fec2_term_t **constant_terms, int n_eq, int n_unknowns);
  int first_non_zero_idx(const uint8_t *a, int n_unknowns);
  void gauss_elimination(int n_eq, int n_unknowns, uint8_t **a, srv6_fec2_term_t **constant_terms, srv6_fec2_term_t **x, bool *undetermined, uint8_t *mul, uint8_t *inv, uint16_t max_packet_length);

  Packet* xor_recover_symbols() CLICK_COLD;
  void xor_one_symbol(srv6_fec2_term_t *rec, Packet *s) CLICK_COLD;
};

CLICK_ENDDECLS
#endif

