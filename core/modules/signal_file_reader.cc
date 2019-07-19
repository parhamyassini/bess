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

#include "spark_common.h"

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
    LOG(INFO) << "Configured template size: " << arg.template_().length();
    //h_size_ = arg.h_size();

    h_size_ = arg.template_().length();

    if(h_size_ >= MAX_TOTAL_PACKET_SIZE){
        LOG(INFO) << "Packet header too larger";
        return CommandFailure(EINVAL, "Template too large");
    }

    bess::utils::Copy(templ_, (const char*)arg.template_().c_str(), h_size_);
    LOG(INFO) << "Got template of: \"" << templ_ << "\"";

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
        //char *ptr = pkt->head_data<char *>(0);

        //int totalLength = pkt->total_len();
        BcdID* bcd_id_val_p = (BcdID*)malloc(sizeof(BcdID));

        bess::utils::Copy(bcd_id_val_p, pkt->head_data<void *>(0), sizeof(BcdID));

        //char pathBuf[PATH_MAX+1];

        /*if(totalLength > PATH_MAX){
            LOG(INFO) << "Error, path too long. Length: " << totalLength;
            // bess::Free(pkt);
            continue;
        }
        else
        {
            LOG(INFO) << "Total length valid: " << totalLength;
        }*/

        // strncpy(pathBuf, ptr, totalLength);
        // pathBuf[totalLength] = '\0';//Should be fixed -- no need to copy here.

        // strcpy(sharedPath_, pathBuf);


        // LOG(INFO) << "Packet contains: " << pathBuf;

        //Now, read that file out
        // if(strlen(pathBuf) == 0){
        //     LOG(INFO) << "Invalid 0 length path.";
        //     continue;
        // }

        //Dynamic array

        char *temporaryPathPtr;// = new char[totalLength+1];//Do this at initialization, don't dynamically allocate
        temporaryPathPtr = (char*)bcd_id_val_p;
        // strcpy(temporaryPathPtr, pathBuf);

        int enqueueRes = llring_enqueue(queue_, temporaryPathPtr);

        LOG(INFO) << "Enqueue result: " << enqueueRes;

        bess::Packet::Free(pkt);
    }
    batch->clear();
}

struct task_result SignalFileReader::RunTask(
  Context *ctx,
  bess::PacketBatch *batch,
  __attribute__((unused)) void *arg) {

    static int lastOpenFd;
    static bool workingOnFd = false;

    struct task_result taskResultVal;


    llring_addr_t ringBufObj;
    bool workToDo = false;

    static int totalSentPackets = 0;

    if(workingOnFd){//In progress
        workToDo = true;
    }
    else if(llring_dequeue(queue_,  &ringBufObj) == 0){
        LOG(INFO) << "Got path from the queue!!!: " << (char*)ringBufObj;
        
        // BcdId* tmp_ptr = ringBufObj;

        int pi = 3;

        BcdID* bcd_id_val_p = (BcdID*)ringBufObj;

        char pathPtr[PATH_MAX];
        BcdIdtoFilename(bcd_id_val_p, pathPtr);

        if((lastOpenFd = open((char*)pathPtr, O_RDONLY)) == -1){
            LOG(INFO) << "Failed to open file \"" << (char*)pathPtr << "\"";
            workToDo = false;
        }
        else
        {
            workToDo = true;
        }
        // LOG(INFO) << "Going to delete: \"" << (char*)pathPtr << "\" " << (void*)pathPtr;
        // delete (char*)ringBufObj;
        free(ringBufObj);
    }


    if(workToDo){
        uint8_t memoryBuf[MAX_TOTAL_PACKET_SIZE];
        ssize_t readSz = -1; 

        unsigned int pktSentCnt = 0u;

        //Allocate the first packet and create a pointer to the data
        bess::Packet *newPkt = current_worker.packet_pool()->Alloc();

        char *newPtr = static_cast<char *>(newPkt->buffer()) + SNBUF_HEADROOM + h_size_;

        // lseek(lastOpenFd, last_path_offset, SEEK_SET);
        while((! batch->full()) && (readSz = read(lastOpenFd, newPtr, MAX_TOTAL_PACKET_SIZE - h_size_)) > 0){

            bess::utils::Copy(newPtr - h_size_, templ_, h_size_);

            newPkt->set_data_len(readSz + h_size_);
            newPkt->set_total_len(readSz + h_size_);

            batch->add(newPkt);
            pktSentCnt++;

            newPkt = current_worker.packet_pool()->Alloc();
            newPtr = static_cast<char *>(newPkt->buffer()) + SNBUF_HEADROOM + h_size_;
        }

        bess::Packet::Free(newPkt);//We are guaranteed to overallocate by one, free this extra packet

        totalSentPackets += pktSentCnt;

        if (readSz == 0) {
            totalSentPackets = 0;
            workingOnFd = false;
            if(close(lastOpenFd) != 0){
                LOG(INFO) << "Error could not close fd " << lastOpenFd;
            }
        }

        if(pktSentCnt > 0){
            RunNextModule(ctx, batch);
        }

        taskResultVal.packets = pktSentCnt;
        taskResultVal.bits = 8*pktSentCnt;
        taskResultVal.block = false;

        
        return taskResultVal;
    }

    taskResultVal.packets = 0u;
    taskResultVal.bits = 0;
    taskResultVal.block = true;

    // LOG(INFO) << "Task Result arg: " << arg << ctx << batch;
    return taskResultVal;
}


ADD_MODULE(SignalFileReader, "signalFileReader", "read a file specified by the signal input")