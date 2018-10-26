#ifndef CLICKNET_ERSPAN_H
#define CLICKNET_ERSPAN_H

struct click_erspan {
#if CLICK_BYTE_ORDER == CLICK_BIG_ENDIAN
    uint8_t ver : 4;
    uint16_t vlan_up : 4;
#else
    uint16_t vlan_up : 4;
    uint8_t ver : 4;
#endif
    uint16_t vlan_down : 8;
#if CLICK_BYTE_ORDER == CLICK_BIG_ENDIAN
    uint8_t cos : 3;
    uint8_t en : 2;
    uint8_t t : 1;
    uint16_t id_up : 2;
#else
    uint16_t id_up : 2;
    uint8_t t : 1;
    uint8_t en : 2;
    uint8_t cos : 3;
#endif
    uint8_t id_down;
    uint16_t _reserved : 12;
    uint32_t index : 20;
} CLICK_SIZE_PACKED_ATTRIBUTE;

struct click_erspan3 {
#if CLICK_BYTE_ORDER == CLICK_BIG_ENDIAN
    uint8_t ver : 4;
    uint16_t vlan_up : 4;
#else
    uint16_t vlan_up : 4;
    uint8_t ver : 4;
#endif
    uint16_t vlan_down : 8;
#if CLICK_BYTE_ORDER == CLICK_BIG_ENDIAN
    uint8_t cos : 3;
    uint8_t bso : 2;
    uint8_t t : 1;
    uint16_t id_up : 2;
#else
    uint16_t id_up : 2;
    uint8_t t : 1;
    uint8_t bso : 2;
    uint8_t cos : 3;
#endif
    uint16_t id_down : 8;
    uint32_t timestamp : 32;
    uint16_t sgt : 16;

#if CLICK_BYTE_ORDER == CLICK_BIG_ENDIAN
    uint8_t p : 1;
    uint8_t ft : 5;
    uint8_t hw_up : 2;

    uint8_t hw_down : 4;
    uint8_t d : 1;
    uint8_t gra : 2;
    uint8_t o : 1;
#else
    uint8_t hw_up : 2;
    uint8_t ft : 5;
    uint8_t p : 1;
    uint8_t o : 1;
    uint8_t gra : 2;
    uint8_t d : 1;
    uint8_t hw_down : 4;

#endif
} CLICK_SIZE_PACKED_ATTRIBUTE;

struct click_erspan3_platform {

#if CLICK_BYTE_ORDER == CLICK_BIG_ENDIAN
    uint8_t platf : 6;
    uint64_t pltaf_info_up : 2;
#else
    uint64_t pltaf_info_up : 2;
    uint8_t platf : 6;
#endif
    uint64_t pltaf_info_down : 56;
} CLICK_SIZE_PACKED_ATTRIBUTE;

struct click_erspan3_platform1 {
    uint8_t platf : 6;
    uint16_t _reserved : 14;
    uint16_t vsm : 12;
    uint32_t id;
}CLICK_SIZE_PACKED_ATTRIBUTE;

/**
 * Timestamp in base is lower bits, here higher bits
 */
struct click_erspan3_platform3 {
#if CLICK_BYTE_ORDER == CLICK_BIG_ENDIAN
    uint8_t platf : 6;
    uint64_t _reserved : 2;
#else
    uint64_t _reserved : 2;
    uint8_t platf : 6;
#endif
    uint16_t _reserved2 : 8;
    uint16_t _reserved3 : 2;
    uint16_t id : 14;
    uint32_t timestamp;
} CLICK_SIZE_PACKED_ATTRIBUTE;

/**
 * Timestamp in base is in 100 ms unit
 */
struct click_erspan3_platform4 {
    uint8_t platf : 6;
    uint16_t _reserved : 12;
    uint16_t _reserved2 : 14;
    uint32_t _reserved3;
} CLICK_SIZE_PACKED_ATTRIBUTE;

/**
 * Timestamp here is seconds
 */
struct click_erspan3_platform56 {
    uint8_t platf : 6;
    uint16_t switchid : 12;
    uint16_t portid : 14;
    uint32_t timestamp;
}CLICK_SIZE_PACKED_ATTRIBUTE;

#endif /* CLICKNET_ERSPAN_H */
