/* -*- mode: c; c-basic-offset: 4 -*- */
#ifndef CLICKNET_GTP_H
#define CLICKNET_GTP_H


struct click_gtp {



    unsigned    gtp_flags : 3;       /*     flags      */
    unsigned    gtp_reserved : 1;       /*      reserved      */
    unsigned    gtp_pt : 1;       /*      protocol type == 1      */
    unsigned    gtp_v : 3;       /*      version == 1            */
#define   GTP_FLAG_E  0x01      /*         extension header flag */
#define   GTP_FLAG_S     0x02      /*         sequence number flag     */
#define   GTP_FLAG_PN     0x04      /*         N-PDU number flag     */
    uint8_t    gtp_msg_type;         /*      GTP message type            */
    uint16_t    gtp_msg_len;          /*      GTP message length          */
    uint32_t    gtp_teid;         /*    Tunnel endpoint identifier       */
};

#endif
