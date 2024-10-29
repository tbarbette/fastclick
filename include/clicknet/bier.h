#ifndef CLICKNET_BIER_H
#define CLICKNET_BIER_H

#include <cstdint>
#ifndef IP6PROTO_BIERIN6
#define IP6PROTO_BIERIN6 253
#endif

#include <arpa/inet.h>
#include <click/bitvector.hh>
#include <click/packet.hh>
#include <math.h>
#include <stdint.h>

typedef uint16_t bfrid;
typedef Bitvector bitstring;

typedef enum: uint8_t { _invalid, _64 = 1, _128, _256, _512, _1024, _2048, _4096 } bsl_t;

#define bier_ttl     bier_un.bier_un2.ttl
#define bier_tc      bier_un.bier_un2.tc
#define bier_s       bier_un.bier_un2.s
#define bier_bift_id bier_un.bier_un2.bift_id
#define bier_nibble  bier_un.bier_un2.nibble
#define bier_version bier_un.bier_un2.version
#define bier_bsl     bier_un.bier_un2.bsl
#define bier_entropy bier_un.bier_un2.entropy
#define bier_oam     bier_un.bier_un2.oam
#define bier_rsv     bier_un.bier_un2.rsv
#define bier_dscp    bier_un.bier_un2.dscp
#define bier_proto   bier_un.bier_un2.proto
#define bier_bfr_id  bier_un.bier_un2.bfr_id

static inline std::size_t _click_bier_expand_bsl(uint8_t bsl) {
  std::size_t ret = 0;
  switch (bsl) {
    case _64: ret = 64; break;
    case _128: ret = 128; break;
    case _256: ret = 256; break;
    case _512: ret = 512; break;
    case _1024: ret = 1024; break;
    case _2048: ret = 2048; break;
    case _4096: ret = 4096; break;
    default: break; 
  }
  return ret;
}

// RFC8296 Section 2
struct click_bier {
  union {
    struct {
      uint32_t bier_un1_l1;
      uint32_t bier_un1_l2;
      uint32_t bier_un1_l3;
    } bier_un1;
    struct {
#if CLICK_BYTE_ORDER == CLICK_BIG_ENDIAN
      unsigned bift_id: 20;
      unsigned tc: 3;
      unsigned s: 1;
      unsigned ttl: 8;
      unsigned nibble: 4;
      unsigned version: 4;
      unsigned bsl: 4;
      unsigned entropy: 20;
      unsigned oam: 2;
      unsigned rsv: 2;
      unsigned dscp: 6;
      unsigned proto: 6;
      unsigned bfr_id: 16;
#elif CLICK_BYTE_ORDER == CLICK_LITTLE_ENDIAN
      unsigned ttl: 8;
      unsigned s: 1;
      unsigned tc: 3;
      unsigned bift_id: 20;
      unsigned entropy: 20;
      unsigned bsl: 4;
      unsigned version: 4;
      unsigned nibble: 4;
      unsigned bfr_id: 16;
      unsigned proto: 6;
      unsigned dscp: 6;
      unsigned rsv: 2;
      unsigned oam: 2;
#endif
    } bier_un2;
  } bier_un;

  uint32_t bitstring[];

  void decode() {
#if CLICK_BYTE_ORDER == CLICK_LITTLE_ENDIAN
    bier_un.bier_un1.bier_un1_l1  = htonl(bier_un.bier_un1.bier_un1_l1); 
    bier_un.bier_un1.bier_un1_l2  = htonl(bier_un.bier_un1.bier_un1_l2); 
    bier_un.bier_un1.bier_un1_l3  = htonl(bier_un.bier_un1.bier_un1_l3);
    // FIXME: Do not use BSL per RFC8296. BSL should be provided as an argument.
    size_t bsl = _click_bier_expand_bsl(bier_bsl) / 32;
    uint32_t _bitstring[bsl];
    for (size_t i=0; i<bsl; i++) _bitstring[bsl-1-i] = htonl(bitstring[i]);
    memcpy(bitstring, _bitstring, bsl*sizeof(uint32_t));
#endif
  }

  void encode() {
#if CLICK_BYTE_ORDER == CLICK_LITTLE_ENDIAN
    // FIXME: Do not use BSL per RFC8296. BSL should be provided as an argument.
    size_t bsl = _click_bier_expand_bsl(bier_bsl) / 32;
    bier_un.bier_un1.bier_un1_l1  = ntohl(bier_un.bier_un1.bier_un1_l1); 
    bier_un.bier_un1.bier_un1_l2  = ntohl(bier_un.bier_un1.bier_un1_l2); 
    bier_un.bier_un1.bier_un1_l3  = ntohl(bier_un.bier_un1.bier_un1_l3); 
    uint32_t _bitstring[bsl];
    for (size_t i=0; i<bsl; i++) _bitstring[bsl-1-i] = ntohl(bitstring[i]);
    memcpy(bitstring, _bitstring, bsl*sizeof(uint32_t));
#endif
  }

} __attribute__ ((aligned (4), packed));

static inline std::size_t click_bier_expand_bsl(click_bier *hdr) {
  return _click_bier_expand_bsl(hdr->bier_bsl);
}

static inline std::size_t click_bier_hdr_len(click_bier *hdr) {
  // FIXME: From RFC8296 about BSL field
  /*  "Note: When parsing the BIER header, a BFR MUST infer the length of
   *   the BitString from the BIFT-id and MUST NOT infer it from the
   *    value of this field.  This field is present only to enable offline
   *   tools (such as LAN analyzers) to parse the BIER header."
   */
  return (3+pow((double)2, (double)hdr->bier_bsl))*sizeof(uint32_t);
}

#endif
