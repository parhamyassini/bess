#include <glog/logging.h>

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
        size_bcd_struct* toQueue = (size_bcd_struct*) malloc(sizeof(size_bcd_struct));
        //BcdID* bcd_id_val_p = (BcdID*)malloc(sizeof(BcdID));
        uint64_t filesize_nbe;
        bess::utils::Copy(&filesize_nbe, pkt->head_data<void *>(0), sizeof(uint64_t));
        toQueue->filesize = be64toh(filesize_nbe);
        //LOG(INFO) << "FS_nbe: " << filesize_nbe;
        //LOG(INFO) << "FS: " << toQueue->filesize;

        bess::utils::Copy(&(toQueue->bcd_id_val), pkt->head_data<char*>(0) + sizeof(uint64_t), sizeof(BcdID));

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
        temporaryPathPtr = (char*)toQueue;
        // strcpy(temporaryPathPtr, pathBuf);

        if(llring_enqueue(queue_, temporaryPathPtr) != 0){
            LOG(INFO) << "Enqueue of path: \"" << temporaryPathPtr << "\" failed";
        }

        //LOG(INFO) << "Enqueue result: " << enqueueRes;

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
    static size_bcd_struct hdrToWrite;


    struct task_result taskResultVal;

    llring_addr_t ringBufObj;
    bool workToDo = false;

    static int totalSentPackets = 0;
    batch->clear();

    if(workingOnFd){//In progress
        workToDo = true;
    }
    else if(llring_dequeue(queue_,  &ringBufObj) == 0){
        //LOG(INFO) << "Got path from the queue!!!: " << (char*)ringBufObj;
        
        // BcdId* tmp_ptr = ringBufObj;

        //BcdID* bcd_id_val_p = (BcdID*)ringBufObj;
        //msgHdr_t* msg_hdr_p = (msgHdr_t*)ringBufObj;

        size_bcd_struct* bcd_size_val_p = (size_bcd_struct*)ringBufObj;
        hdrToWrite = *bcd_size_val_p;
        free((void*)ringBufObj);

        char pathPtr[PATH_MAX];
        BcdIdtoFilename( BCD_DIR_PREFIX, &(hdrToWrite.bcd_id_val), pathPtr);
        LOG(INFO) << "Got path from the queue!!!: " << pathPtr;
        LOG(INFO) << "Got filesize from queue!!!: " << (hdrToWrite.filesize);

        //char temp[1000] = {0};
        //sprintf(temp,"echo \"a2b84498f4c2835e5e0065f1d56d45c7\" > %s", pathPtr);
        //int tempRet = 0;
        //tempRet += system(temp);
        //for(int i = 0 ; i < 1000; i++){
        //    sprintf(temp,"head -c 20 /dev/urandom | md5sum | cut -d ' ' -f 1 >> %s", pathPtr);
        //    tempRet = system(temp);
        //}
        //LOG(INFO) << "Command: " << temp << " returned " << tempRet;
        //hdrToWrite.filesize = strlen("a2b84498f4c2835e5e0065f1d56d45c7\n") * 1001;

        //char temp[1000] = {0};
        //sprintf(temp,"head -c 200 /dev/urandom > %s", pathPtr);
        //int tempRet = 0;
        //tempRet += system(temp);
        //for(int i = 0 ; i < 100; i++){
        //    sprintf(temp,"head -c 200 /dev/urandom >> %s", pathPtr);
        //    tempRet = system(temp);
        //}
        //LOG(INFO) << "Command: " << temp << " returned " << tempRet;
        //hdrToWrite.filesize = 200 * 101;

        //sleep(2);


        if((lastOpenFd = open((char*)pathPtr, O_RDONLY)) == -1){
            LOG(INFO) << "Failed to open file \"" << (char*)pathPtr << "\"";
            workToDo = false;
        }
        else
        {
            workToDo = true;

            struct stat sBuf;
            fstat(lastOpenFd, &sBuf);
            hdrToWrite.filesize = sBuf.st_size;
            LOG(INFO) << "Hdr filesize is: " << hdrToWrite.filesize << " rather than: " << 200*101;
        }
        // LOG(INFO) << "Going to delete: \"" << (char*)pathPtr << "\" " << (void*)pathPtr;
        // delete (char*)ringBufObj;

        //free(ringBufObj);
    }


    if(workToDo && ! batch->full()){
        workingOnFd = true;
        ssize_t readSz = -1; 

        unsigned int pktSentCnt = 0u;

        size_t total_h_size = h_size_ + sizeof(BcdID) + sizeof(uint64_t);

        //Allocate the first packet and create a pointer to the data
        bess::Packet *newPkt = current_worker.packet_pool()->Alloc();

        char *startPtr = static_cast<char *>(newPkt->head_data(0));

        //LOG(INFO) << "Packet length is: " << total_h_size << ", h_size: " << h_size_;
        //        LOG(INFO) << "The batch already has: " << batch->cnt() << " elements.";

        memset(startPtr, '1', 1500);
        newPkt->set_data_len(total_h_size+56);
        newPkt->set_total_len(total_h_size+56);
        //LOG(INFO) << "Original packet: " << newPkt->Dump();
        while((! batch->full()) && (readSz = read(lastOpenFd,startPtr + total_h_size, MAX_TOTAL_PACKET_SIZE - total_h_size)) > 0){
            if(h_size_ > 0){
                bess::utils::Copy(startPtr, templ_, h_size_);//Always include the constant template we are given
            }
            bess::utils::Copy(startPtr + h_size_, &(hdrToWrite.filesize), sizeof(uint64_t));
            bess::utils::Copy(startPtr + h_size_ + sizeof(uint64_t), &(hdrToWrite.bcd_id_val), sizeof(BcdID));

            //LOG(INFO) << "readSz: " << readSz << " packet size: " << total_h_size;
            newPkt->set_data_len(readSz + total_h_size);
            newPkt->set_total_len(readSz + total_h_size);
            //LOG(INFO) << "Sending packet: " << newPkt->Dump();
            batch->add(newPkt);
            pktSentCnt++;

            newPkt = current_worker.packet_pool()->Alloc();
            startPtr = static_cast<char *>(newPkt->head_data(0));
        }

        bess::Packet::Free(newPkt);//We are guaranteed to overallocate by one, free this extra packet

        totalSentPackets += pktSentCnt;
        //LOG(INFO) << "SLEEP SLEEP SLEEP";
        //sleep(1);

        if (readSz == 0) {
            LOG(INFO) << "totalSentPackets: " << totalSentPackets;
            workingOnFd = false;
            if(close(lastOpenFd) != 0){
                LOG(INFO) << "Error could not close fd " << lastOpenFd;
            }
        }

        //LOG(INFO) << "From the batch. cnt: " << batch->cnt() << " data: "  << batch->pkts()[0]->Dump();
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
