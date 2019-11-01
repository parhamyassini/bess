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

#include "spark_common.h"



class SparkInterface final : public Module {
    public:
        static const gate_idx_t kNumIGates = 2;
        static const gate_idx_t kNumOGates = 2;
        CommandResponse Init(const bess::pb::SparkInterfaceArg &arg);

        void ProcessBatch(Context *ctx, bess::PacketBatch *batch);

    private:
        uint8_t addMsgToQueue(BcdID *bcd_id_p, msg_type_t type, char *msg_payload, bytes_t msg_payload_len, uint8_t padding, Context *ctx);
        void SendToFileGate(uint64_t filesize, char* data, int msg_len, Context *ctx);
        bool isFileExist(char *);
        void DoProcessSocketBatch(Context *ctx, bess::PacketBatch *batch);
        void DoProcessFileWriterBatch(bess::PacketBatch *batch);
        int32_t h_size_ = 0;
        uint64_t size_;
        uint64_t file_size_;
        std::unordered_set <std::string> saved_files_set;
        gate_idx_t spark_gate_;
        gate_idx_t file_gate_;

};

#endif /* BESS_MODULES_SIGNAL_FILE_READER_H_ */
