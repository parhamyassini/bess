#ifndef BESS_MODULES_SIGNAL_FILE_READER_H_
#define BESS_MODULES_SIGNAL_FILE_READER_H_

#include "../module.h"

#include <limits.h>
#include "../kmod/llring.h"


#include <endian.h>
#include <sys/types.h>
#include <string>


#include <glog/logging.h>

#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#include "packet.h"

#include "../port.h"



#define MAX_PKT_LEN (1500)
#define STD_HDR_LEN (48)//Was 52


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


class SparkInterface final : public Module {
    public:
        CommandResponse Init(const bess::pb::SparkInterfaceArg &arg);

        void ProcessBatch(Context *ctx, bess::PacketBatch *batch);
        struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
            void *arg) override;

    private:
        int addMsgToQueue(BcdID *bcd_id_p, msg_type_t type, char *msg_payload, bytes_t msg_payload_len, uint8_t padding);
        
        //Variables
        int Resize(int slots, struct llring** queue_pp, uint64_t *new_size_p);
        struct llring *queue_;
        struct llring *file_queue_;
        // char sharedPath_[PATH_MAX + 1];
        // Queue capacity
		uint64_t size_;
		uint64_t file_size_;

        gate_idx_t spark_gate_;
        gate_idx_t file_gate_;
};

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

#endif /* BESS_MODULES_SIGNAL_FILE_READER_H_ */
