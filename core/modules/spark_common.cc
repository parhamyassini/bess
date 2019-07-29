#include "spark_common.h"

void parseMsgHdr(msgHdr_t* msgHdrData, uint8_t* ptr)
{
        msgHdrData->len_dat = *((bess::utils::be32_t*)(ptr + 0));
        msgHdrData->msb_dat = *((bess::utils::be64_t*)(ptr + 4));
        msgHdrData->lsb_dat = *((bess::utils::be64_t*)(ptr + 12));
        msgHdrData->id_dat = *((bess::utils::be64_t*)(ptr + 20));
        msgHdrData->type_dat = *((bess::utils::be32_t*)(ptr + 28));
        msgHdrData->time_sec_dat = *((bess::utils::be64_t*)(ptr + 32));
        msgHdrData->time_usec_dat = *((bess::utils::be64_t*)(ptr + 40));
}

void createBcdId(char *buf_p, const BcdID * bcd_id_p) {
    BcdID *p = (BcdID *) buf_p;
    p->app_id.msb = htobe64(bcd_id_p->app_id.msb);
    p->app_id.lsb = htobe64(bcd_id_p->app_id.lsb);
    p->data_id = htobe64(bcd_id_p->data_id);
    return;
}

void createMsgHdr(char *buf_p, const BcdID *bcd_id_p, const msg_type_t type, const bytes_t len) {
    MsgHdr *p = (MsgHdr *) buf_p;
    createBcdId((char *) &(p->bcd_id), bcd_id_p);
    p->type = htonl(type);
    p->len = htonl(len);
    gettimeofday(&(p->tv), NULL);
    return;
}

bytes_t createMsg(char *buf_p, const BcdID *bcd_id_p, const msg_type_t type, const char *msg_payload, const bytes_t tcp_payload_len, const bytes_t padding_len) {
    createMsgHdr(buf_p, bcd_id_p, type, tcp_payload_len + padding_len); /* assign the message header */
    if (msg_payload) { /* copy the payload */
        memcpy(buf_p + MSG_HDRLEN, msg_payload, tcp_payload_len - MSG_HDRLEN);
    }
    return tcp_payload_len;
}

static MsgToken *_createMsgToken(const bytes_t tcp_payload_len, uint8_t padding) {
    MsgToken *mt_p = (MsgToken *) malloc(sizeof(MsgToken)); /* initiate the msg tuple */
    mt_p->buf_p = (char *) malloc(tcp_payload_len); /* msg buffer for the entire message including the msg header and the payload */
    mt_p->msg_len = tcp_payload_len;
    mt_p->padding_len = padding ? (INTERFACE_MTU - IP4_HDRLEN - TCP_HDRLEN - mt_p->msg_len) : 0;
    mt_p->byte_sent = 0;
    gettimeofday(&(mt_p->tv), NULL);
    return mt_p;
}

MsgToken * createMsgToken(BcdID *bcd_id_p, msg_type_t type, char *msg_payload, bytes_t msg_payload_len, uint8_t padding) {
    bytes_t tcp_payload_len = MSG_HDRLEN + msg_payload_len; // msgLen is the bytes in TCP payload
	LOG(INFO) << "TCP Payload len: " << tcp_payload_len;
    MsgToken * mt_p = _createMsgToken(tcp_payload_len, padding);
    createMsg(mt_p->buf_p, bcd_id_p, type, msg_payload, tcp_payload_len, mt_p->padding_len);
    return mt_p;
}

void deleteMsgToken(MsgToken *mt_p) {
    if (!mt_p) {
        // D("msgTuple is NULL");
        return;
    }
    free(mt_p->buf_p);
    free(mt_p);
    return;
}

void BcdIdtoFilename(const char* prefix, const BcdID *bcd_id_p, char *buf_p) {
    sprintf(buf_p, "%sbroadcast-%08lx-%04lx-%04lx-%04lx-%012lx/broadcast_%ld", prefix, ((bcd_id_p->app_id.msb) & (0xffffffff00000000)) >> 32, ((bcd_id_p->app_id.msb) & (0x00000000ffff0000)) >> 16,
        (bcd_id_p->app_id.msb) & (0x000000000000ffff), ((bcd_id_p->app_id.lsb) & (0xffff000000000000)) >> 48, (bcd_id_p->app_id.lsb) & (0x0000ffffffffffff), bcd_id_p->data_id);
    return;
}
