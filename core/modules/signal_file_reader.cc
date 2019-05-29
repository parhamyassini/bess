#include <glog/logging.h>

// #include <cassert>
// #include <cctype>
// #include <cerrno>
// #include <cstdio>
// #include <initializer_list>
// #include <memory>
// #include <sstream>
#include <string>


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <limits.h>


#include "signal_file_reader.h"
#include "packet.h"

// #include "../module.h"

#define MAX_TOTAL_PACKET_SIZE 1500u

/*
struct task_result SignalFileReader::RunTask(void *arg) {
    LOG(INFO) << "Running task at: " << arg;
    struct task_result retVal;
    return retVal;
}*/

// CommandResponse SignalFileReader::Init(const bess::pb::SignalFileReaderArg &arg) {
//     LOG(INFO) << "This is the Signal File Reader module INIT";
//     LOG(INFO) << "Configured arg h_size: " << arg.h_size();
//     return CommandSuccess();
// }

// void SignalFileReader::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
//     LOG(INFO) << "Process Batch " << ctx << " " << batch;
//     return;
// }

// SignalFileReader::Init(bess::pb::SignalFileReaderArg const&)
CommandResponse SignalFileReader::Init(const bess::pb::SignalFileReaderArg &arg){
    LOG(INFO) << "This is the Signal File Reader module INIT";
    LOG(INFO) << "Configured arg h_size: " << arg.h_size();

    char pathToSet[] = "/this/is/the/path";

    LOG(INFO) << "Setting path: " << pathToSet;
    strcpy(sharedPath_, pathToSet);
    LOG(INFO) << "Path Set. Setting ready...";
    newPathReady_ = 0;


    RegisterTask(nullptr);

    return CommandSuccess();
}

void SignalFileReader::ProcessBatch(Context *ctx, bess::PacketBatch *batch){
    LOG(INFO) << "BATCH: " << batch << " cnt " << batch->cnt();
    LOG(INFO) << "CONTEXT: " << ctx;
    newPathReady_ = 1;


    //bess::PacketBatch *new_batch = ctx->task->AllocPacketBatch();
    //new_batch->Copy(batch);
    //RunNextModule(ctx, new_batch);

    //Process the packet to open the file, place that onto the queue
    for(int i = 0; i < batch->cnt(); i++){
        bess::Packet *pkt = batch->pkts()[i];
        char *ptr = pkt->buffer<char *>() + SNBUF_HEADROOM;

        int totalLength = pkt->total_len();

        char pathBuf[PATH_MAX+1];

        if(totalLength > PATH_MAX){
            LOG(INFO) << "Error, path too long. Length: " << totalLength;
            // bess::Free(pkt);
            continue;
        }
        else
        {
            LOG(INFO) << "Total length valid: " << totalLength;
        }

        strncpy(pathBuf, ptr, totalLength);
        pathBuf[totalLength] = '\0';


        LOG(INFO) << "Packet contains: " << pathBuf;

        //Now, read that file out
        if(strlen(pathBuf) == 0){
            LOG(INFO) << "Invalid 0 length path.";
            continue;
        }
        int read_fd;

        if((read_fd = open(pathBuf, O_RDONLY)) == -1){
            LOG(INFO) << "Failed to open file \"" << pathBuf << "\"";
            continue;
        }

        // bess::PacketBatch buf;
        // bess::Packet **p_buf = &buf.pkts()[buf->cnt()];
        // int free_slots = bess::PacketBatch::kMaxBurst - buf->cnt();

        uint8_t memoryBuf[MAX_TOTAL_PACKET_SIZE];
        ssize_t readSz; 
        while((readSz = read(read_fd, memoryBuf, MAX_TOTAL_PACKET_SIZE)) > 0){
            //Allocate that packet and process it.
            bess::Packet *newPkt = current_worker.packet_pool()->Alloc();

            // LOG(INFO) << "Allocated pkt: " << newPkt;
            // buf.add(pkt);
            char *newPtr = static_cast<char *>(newPkt->buffer()) + SNBUF_HEADROOM;

            memcpy(newPtr, memoryBuf, readSz);
            // newPkt->copy(pkt);
            // LOG(INFO) << "New pkt " << newPkt->Dump();
            // LOG(INFO) << "Orig pkt " << pkt->Dump();

            newPkt->set_data_len(readSz);
            newPkt->set_total_len(readSz);

            // LOG(INFO) << "Fixed pkt " << newPkt->Dump();

            // bess::PacketBatch *new_batch = ctx->task->AllocPacketBatch();
            // new_batch->add(newPkt);
            // new_batch->Copy(&buf);
            // buf.clear();
            // RunNextModule(ctx, new_batch);
            EmitPacket(ctx, newPkt, 0);

            // buf.clear();
        }

        close(read_fd);
    }
}

struct task_result SignalFileReader::RunTask(__attribute__((unused)) Context *ctx, __attribute__((unused))bess::PacketBatch *batch,
__attribute__((unused)) void *arg){
    // (void*)ctx;
    // (void*)batch;
    // (void*)arg;
    struct task_result x;
    x.packets = 0u;
    x.bits = 0;
    // LOG(INFO) << "Task Result arg: " << arg << ctx << batch;
    return x;
}


ADD_MODULE(SignalFileReader, "signalFileReader", "read a file specified by the signal input")