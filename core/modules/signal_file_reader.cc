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

#include "signal_file_reader.h"
#include "packet.h"

// #include "../module.h"

#define MAX_TOTAL_PACKET_SIZE (1250)

int SignalFileReader::Resize(int slots) {
  struct llring *old_queue = queue_;
  struct llring *new_queue;

  int bytes = llring_bytes_with_slots(slots);

  new_queue =
      reinterpret_cast<llring *>(aligned_alloc(alignof(llring), bytes));
  if (!new_queue) {
    return -ENOMEM;
  }

  int ret = llring_init(new_queue, slots, 0, 1);
  if (ret) {
    std::free(new_queue);
    LOG(INFO) << "Init llring failed with ret: " << ret;
    return -EINVAL;
  }

  /* migrate packets from the old queue */
  if (old_queue) {
    bess::Packet *pkt;

    while (llring_sc_dequeue(old_queue, (void **)&pkt) == 0) {
      ret = llring_sp_enqueue(new_queue, pkt);
      if (ret == -LLRING_ERR_NOBUF) {
        bess::Packet::Free(pkt);
      }
    }

    std::free(old_queue);
  }

  queue_ = new_queue;
  size_ = slots;

  return 0;
}





CommandResponse SignalFileReader::Init(const bess::pb::SignalFileReaderArg &arg){
    LOG(INFO) << "This is the Signal File Reader module INIT";
    LOG(INFO) << "Configured arg h_size: " << arg.h_size();

    int ret = Resize(64);
    if(ret){
        return CommandFailure(-ret);
    }



    if(RegisterTask(nullptr) == INVALID_TASK_ID){
        return CommandFailure(ENOMEM, "Task creation failed");
    }

    return CommandSuccess();
}

void SignalFileReader::ProcessBatch(__attribute__((unused)) Context *ctx, bess::PacketBatch *batch){
    //bess::PacketBatch *new_batch = ctx->task->AllocPacketBatch();
    //new_batch->Copy(batch);
    //RunNextModule(ctx, new_batch);

    //Process the packet to open the file, place that onto the queue
    for(int i = 0; i < batch->cnt(); i++){
        bess::Packet *pkt = batch->pkts()[i];
        // char *ptr = pkt->buffer<char *>() + SNBUF_HEADROOM;
        char *ptr = pkt->head_data<char *>(0);

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
        pathBuf[totalLength] = '\0';//Should be fixed -- no need to copy here.

        // strcpy(sharedPath_, pathBuf);


        LOG(INFO) << "Packet contains: " << pathBuf;

        //Now, read that file out
        if(strlen(pathBuf) == 0){
            LOG(INFO) << "Invalid 0 length path.";
            continue;
        }

        //Dynamic array

        char *temporaryPathPtr = new char[totalLength+1];//Do this at initialization, don't dynamically allocate
        strcpy(temporaryPathPtr, pathBuf);

        int enqueueRes = llring_enqueue(queue_, temporaryPathPtr);

        LOG(INFO) << "Enqueue result: " << enqueueRes;
    }
    batch->clear();
}

struct task_result SignalFileReader::RunTask(
  Context *ctx,
  bess::PacketBatch *batch,
  __attribute__((unused)) void *arg) {

    static char local_working_path[PATH_MAX + 1];
    static off_t last_path_offset = 0;
    struct task_result taskResultVal;


    llring_addr_t ringBufObj;
    bool workToDo = false;

    static int totalSentPackets = 0;

    if(last_path_offset != 0){//In progress
        workToDo = true;
    }
    else if(llring_dequeue(queue_,  &ringBufObj) == 0){
        LOG(INFO) << "Got path from the queue!!!: " << (char*)ringBufObj;
        strncpy(local_working_path, (char*)ringBufObj, PATH_MAX);
        // delete (void*)ringBufObj;
        workToDo = true;
    }


    // llring_addr_t ringBufObj;
    // int dequeueRes = llring_dequeue(queue_,  ringBufObj);
    if(workToDo){
        int read_fd;

        if((read_fd = open(local_working_path, O_RDONLY)) == -1){
            LOG(INFO) << "Failed to open file \"" << local_working_path << "\"";
            goto exitEmpty;
        }

        uint8_t memoryBuf[MAX_TOTAL_PACKET_SIZE];
        ssize_t readSz = -1; 

        unsigned int pktSentCnt = 0u;

        lseek(read_fd, last_path_offset, SEEK_SET);
        while(/*(readSz != 0) && */(! batch->full()) && (readSz = read(read_fd, memoryBuf, MAX_TOTAL_PACKET_SIZE)) > 0){
            //Allocate that packet and process it.
            last_path_offset += readSz;
            bess::Packet *newPkt = current_worker.packet_pool()->Alloc();

            // LOG(INFO) << "Allocated pkt: " << newPkt;
            // buf.add(pkt);
            char *newPtr = static_cast<char *>(newPkt->buffer()) + SNBUF_HEADROOM;

            memcpy(newPtr, memoryBuf, readSz);//Read directly into the packet -- don't copy


            newPkt->set_data_len(readSz);
            newPkt->set_total_len(readSz);

            batch->add(newPkt);
            // RunNextModule(ctx, batch);
            pktSentCnt++;
        }
        totalSentPackets += pktSentCnt;
        if (readSz == 0) {
            // LOG(INFO) << "readSz == 0";
            last_path_offset = 0;
            LOG(INFO) << "Total packets sent: " << totalSentPackets;
            totalSentPackets = 0;
        }
        // LOG(INFO) << "Packet send cnt: " << pktSentCnt;

        if(pktSentCnt > 0){
            RunNextModule(ctx, batch);
        }

        taskResultVal.packets = pktSentCnt;
        taskResultVal.bits = 8*pktSentCnt;
        taskResultVal.block = false;

        close(read_fd);
        return taskResultVal;
    }

exitEmpty:
    taskResultVal.packets = 0u;
    taskResultVal.bits = 0;
    taskResultVal.block = true;

    // LOG(INFO) << "Task Result arg: " << arg << ctx << batch;
    return taskResultVal;
}


ADD_MODULE(SignalFileReader, "signalFileReader", "read a file specified by the signal input")