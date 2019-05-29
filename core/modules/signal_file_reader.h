#ifndef BESS_MODULES_SIGNAL_FILE_READER_H_
#define BESS_MODULES_SIGNAL_FILE_READER_H_

#include "../module.h"

class SignalFileReader final : public Module {
    public:
        CommandResponse Init(const bess::pb::SignalFileReaderArg &arg);
        
        void ProcessBatch(Context *ctx, bess::PacketBatch *batch);
        struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
void *arg) override;

    private:
        struct llring *queue_;
        char sharedPath_[100];
        int8_t newPathReady_;
};

#endif /* BESS_MODULES_SIGNAL_FILE_READER_H_ */