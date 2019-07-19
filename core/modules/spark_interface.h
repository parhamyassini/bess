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
        static const gate_idx_t kNumIGates = 1;
        static const gate_idx_t kNumOGates = 2;
        CommandResponse Init(const bess::pb::SparkInterfaceArg &arg);

        void ProcessBatch(Context *ctx, bess::PacketBatch *batch);
        //struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
        //    void *arg) override;

    private:
        int addMsgToQueue(BcdID *bcd_id_p, msg_type_t type, char *msg_payload, bytes_t msg_payload_len, uint8_t padding, Context *ctx);
        void SendToFileGate(char* data, int msg_len, Context *ctx);
        
        //Variables
        //int Resize(int slots, struct llring** queue_pp, uint64_t *new_size_p);
        //struct llring *queue_;
        //struct llring *file_queue_;
        // char sharedPath_[PATH_MAX + 1];
        // Queue capacity
		uint64_t size_;
		uint64_t file_size_;

        gate_idx_t spark_gate_;
        gate_idx_t file_gate_;
};

#endif /* BESS_MODULES_SIGNAL_FILE_READER_H_ */
