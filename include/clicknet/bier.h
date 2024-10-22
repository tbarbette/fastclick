#ifndef CLICKNET_BIER_H
#define CLICKNET_BIER_H

#ifndef IP6PROTO_BIERIN6
#define IP6PROTO_BIERIN6 253
#endif

#include <click/bitvector.hh>
#include <stdint.h>

typedef uint16_t bfrid;
typedef Bitvector bitstring; // bslen == 256


// RFC8296 Section 2
struct click_bier {
  unsigned bift_id : 20;
  unsigned tc : 3;
  unsigned s : 1;
  uint8_t ttl;
  unsigned nibble : 4;
  unsigned version : 4;
  unsigned bsl : 4;
  unsigned entropy : 20;
  unsigned oam : 2;
  unsigned _rsv : 2;
  unsigned dscp : 6;
  unsigned proto : 6;
  unsigned bfr_id : 16;
  uint64_t bitstring;
};

#endif
