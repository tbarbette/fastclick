/* -*- mode: c; c-basic-offset: 4 -*- */
#ifndef GRE_H
#define GRE_H
#include <click/cxxprotect.h>
CLICK_CXX_PROTECT

/*
 * our own definitions of GRE headers
 * based on a file from one of the BSDs
 */

#define GRE_CP          0x8000  /* Checksum Present */
#define GRE_RP          0x4000  /* Routing Present */
#define GRE_KP          0x2000  /* Key Present */
#define GRE_SP          0x1000  /* Sequence Present */
#define GRE_SS		0x0800	/* Strict Source Route */
#define GRE_VERSION     0x0007  /* Version Number */

struct click_gre {
    uint16_t flags;		/* See above */
    uint16_t protocol;		/* Ethernet protocol type */
    uint32_t options[3];	/* Optional fields (up to 12 bytes in GRE version 0) */
};

CLICK_CXX_UNPROTECT
#include <click/cxxunprotect.h>
#endif
