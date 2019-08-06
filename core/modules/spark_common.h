#ifndef SPARK_COMMON_H
#define SPARK_COMMON_H

#define MAX_PKT_LEN (1500)
#define STD_HDR_LEN (48)//Was 52

#include "../port.h"
#include "endian.h"
#include <arpa/inet.h>

//typedef struct _msgBytes {
//    char pkt[MAX_PKT_LEN];
//    int len;
//} msgBytes;


/* primitive types */
typedef uint32_t seqnum_t;
typedef int64_t databyte_t;
typedef uint64_t app_id_sb_t;
typedef uint64_t data_id_t;
typedef uint32_t msg_type_t;
typedef uint32_t bytes_t;
typedef int32_t pktnum_t;

typedef struct _msg_hdr{
        bess::utils::be32_t len_dat;
        bess::utils::be64_t msb_dat;
        bess::utils::be64_t lsb_dat;
        bess::utils::be64_t id_dat;
        bess::utils::be32_t type_dat;
        bess::utils::be64_t time_sec_dat;
        bess::utils::be64_t time_usec_dat;
} msgHdr_t;

typedef struct _appID {
    app_id_sb_t msb;
    app_id_sb_t lsb;
} AppID;

typedef struct _bcdID {
    AppID app_id;
    data_id_t data_id;
} BcdID;

typedef struct _dataHdr {
    BcdID bcd_id;
    seqnum_t seq_num;
    bytes_t len;
} DataHdr;

typedef struct _msgHdr {
    BcdID bcd_id;
    msg_type_t type;
    bytes_t len;
    struct timeval tv;
} MsgHdr;

typedef struct _msgToken {
	char *buf_p; // pointer to the content of the message. malloc() when MsgToken is created, free() when MsgToken is deleted
	bytes_t msg_len; // length of the message body
	bytes_t padding_len; // length of padding in this message
	bytes_t byte_sent; // # bytes has been sent
	struct timeval tv; // timeval when MsgToken is created (stats only)
} MsgToken;

typedef struct _msgReadRpl { /* reply to the local socket */
	uint32_t code; // the status of the write request
	uint32_t padding;
	databyte_t start;     // the byte pointer of the first byte in the received file
	databyte_t jump;      //
	databyte_t file_size;
} MsgReadRpl;

typedef struct _msgDelRpl { /* reply to the local socket */
	uint32_t code; // the status of the write request
} MsgDelRpl;


typedef struct _size_bcd_struct {
    uint64_t filesize;
    BcdID bcd_id_val;
} size_bcd_struct;


#define MSG_HDRLEN          (sizeof(MsgHdr))
#define DATA_HDRLEN         (sizeof(DataHdr))
#define DATA_HDR_OFFSET     (ETH_HLEN + IP4_HDRLEN + UDP_HDRLEN)
#define DATA_PAYLOAD_OFFSET (ETH_HLEN + IP4_HDRLEN + UDP_HDRLEN + DATA_HDRLEN)

#define IP4_HDRLEN      20  // IPv4 header length
#define IP4_HDRLEN_MAX  60  // IPv4 header length
#define TCP_HDRLEN      20  // TCP header length
#define TCP_HDRLEN_MAX  60  // TCP header length
#define UDP_HDRLEN      8  // UDP header length, excludes data
#define UDP_HDRLEN_MAX  8  // UDP header length, excludes data

#define INTERFACE_MTU           (8946)

#define BCD_DIR_PREFIX			"/mnt/ramdisk/"
#define FILENAME_LEN			(128)



/* message between UNIX domain socket and agent*/
#define MSG_INT_REQ				(90)
#define MSG_INT_RPL				(91)
#define MSG_WRT_REQ				(92)
#define MSG_WRT_RPL				(93)
#define MSG_PSH_REQ				(94)
#define MSG_PSH_RPL				(95)
#define MSG_READ_REQ 			(96)
#define MSG_READ_RPL 			(97)
#define MSG_DEL_REQ				(98)
#define MSG_DEL_RPL				(99)

/* reply code */
#define REPLY_SUCCESS			(50)
#define REPLY_FAIL				(51)
#define REPLY_ALTER				(52)


void parseMsgHdr(msgHdr_t* msgHdrData, uint8_t* ptr);

void createBcdId(char *buf_p, const BcdID * bcd_id_p);

void createMsgHdr(char *buf_p, const BcdID *bcd_id_p, const msg_type_t type, const bytes_t len);

bytes_t createMsg(char *buf_p, const BcdID *bcd_id_p, const msg_type_t type, const char *msg_payload, const bytes_t tcp_payload_len, const bytes_t padding_len);

static MsgToken *_createMsgToken(const bytes_t tcp_payload_len, uint8_t padding);

MsgToken * createMsgToken(BcdID *bcd_id_p, msg_type_t type, char *msg_payload, bytes_t msg_payload_len, uint8_t padding);

void deleteMsgToken(MsgToken *mt_p);

void BcdIdtoFilename(const char* prefix, const BcdID *bcd_id_p, char *buf_p);

bool cmpBcd(BcdID *a, BcdID *b);

#endif /*SPARK_COMMON_H*/
