#ifndef BESS_MODULES_SIGNAL_FILE_READER_H_
#define BESS_MODULES_SIGNAL_FILE_READER_H_

#include "../module.h"

#include <limits.h>
#include "../kmod/llring.h"

#define MAX_TOTAL_PACKET_SIZE (1250)

class SignalFileReader final : public Module {
    public:
        CommandResponse Init(const bess::pb::SignalFileReaderArg &arg);
        int Resize(int slots);

        void ProcessBatch(Context *ctx, bess::PacketBatch *batch);
        struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
void *arg) override;

    private:
        struct llring *queue_;
        // char sharedPath_[PATH_MAX + 1];
        // Queue capacity
        uint64_t size_;
        unsigned int h_size_;
        uint8_t templ_[MAX_TOTAL_PACKET_SIZE];

};

#endif /* BESS_MODULES_SIGNAL_FILE_READER_H_ */
