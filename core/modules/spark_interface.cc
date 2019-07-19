#include "spark_interface.h"

static std::string HexDump(const void *buffer, size_t len);

void SparkInterface::SendToFileGate(char* data, int msg_len, Context *ctx){
    bess::Packet *newPkt = current_worker.packet_pool()->Alloc();
    int i = 0;

    char *newPtr = static_cast<char *>(newPkt->buffer()) + SNBUF_HEADROOM;

    //Now we have to actually emit this data back to spark!
    //memcpy(newPtr, memoryBuf, readSz);//Read directly into the packet -- don't copy
    memcpy(newPtr, data, msg_len);

    newPkt->set_data_len(msg_len);
    newPkt->set_total_len(msg_len);
    LOG(INFO) << "sendToFileGate: " << newPkt->Dump();
    EmitPacket(ctx, newPkt, file_gate_);
}



CommandResponse SparkInterface::Init(const bess::pb::SparkInterfaceArg &arg){
    spark_gate_ = arg.spark_gate();
    file_gate_ = arg.file_gate();

    LOG(INFO) << "Spark gate: " << spark_gate_ << " File gate: " << file_gate_;
    return CommandSuccess();
}


int SparkInterface::addMsgToQueue(BcdID *bcd_id_p, msg_type_t type, char *msg_payload, bytes_t msg_payload_len, uint8_t padding, Context *ctx) {

    LOG(INFO) << "Sending respone by addMsgToQueue function";
    MsgToken *mt_p = createMsgToken(bcd_id_p, type, msg_payload, msg_payload_len, padding);

    bess::Packet *newPkt = current_worker.packet_pool()->Alloc();
    if(newPkt == NULL){
        return -1;
    }
    //return llring_enqueue(queue_, mt_p);
    char *newPtr = static_cast<char *>(newPkt->buffer()) + SNBUF_HEADROOM;
    memcpy(newPtr, mt_p->buf_p, mt_p->msg_len);
    newPkt->set_data_len(mt_p->msg_len);
    newPkt->set_total_len(mt_p->msg_len);

    //LOG(INFO) << "Going to send: " << newPkt->Dump();
    deleteMsgToken(mt_p);
    //llring_enqueue(queue_, mt_p);

    EmitPacket(ctx, newPkt,  spark_gate_);
    return 0;
}

void SparkInterface::ProcessBatch(__attribute__((unused)) Context *ctx, bess::PacketBatch *batch){
    //bess::PacketBatch *new_batch = ctx->task->AllocPacketBatch();
    //new_batch->Copy(batch);
    //batch->clear();
    //RunNextModule(ctx, new_batch);

    //Process the packet to open the file, place that onto the queue
    //
    //
    int messagesSentCount = 0;

    for(int i = 0; i < batch->cnt(); i++){
        bess::Packet *pkt = batch->pkts()[i];
        // char *ptr = pkt->buffer<char *>() + SNBUF_HEADROOM;
        uint8_t *ptr = pkt->head_data<uint8_t*>(0);

        int totalLength = pkt->total_len();

        //First, let's read the standard parameter
        //
        //
        //
        msgHdr_t msgHdr_dat;
        parseMsgHdr(&msgHdr_dat, ptr);

        bess::Packet::Free(pkt);


        //LOG(INFO) << "recvd len: " <<  msgHdr_dat.len_dat.value() << " total length: " << totalLength;
        //LOG(INFO) << "MSB data recv: " << std::hex <<  msgHdr_dat.msb_dat.value() << " LSB data recv: " << std::hex <<  msgHdr_dat.lsb_dat.value();
        //LOG(INFO) << "id data recv: " <<  msgHdr_dat.id_dat.value() << " type data recv: " <<  msgHdr_dat.type_dat.value();
        //LOG(INFO) << "time_sec_dat: " <<  msgHdr_dat.time_sec_dat.value() << " time_usec_dat: " <<  msgHdr_dat.time_usec_dat.value();

        uint8_t buf[MAX_PKT_LEN - STD_HDR_LEN];
        memcpy(buf, ptr+STD_HDR_LEN, msgHdr_dat.len_dat.value() - STD_HDR_LEN);

        //LOG(INFO) << "Memory sent: " << HexDump(buf, msgHdr_dat.len_dat.value() - STD_HDR_LEN);

        //msgBytes* responseMsg = NULL;
        MsgToken* responseMsg = NULL;
        BcdID bcd_id_val;
        bcd_id_val.app_id  = {msgHdr_dat.msb_dat.value(), msgHdr_dat.lsb_dat.value()};
        bcd_id_val.data_id = msgHdr_dat.id_dat.value();

		static int numberOfWrts = 0;

		//bess::utils::be32_t responseCode;//(REPLY_SUCCESS);
		int responseCode;
        switch(msgHdr_dat.type_dat.value()){
            case (MSG_INT_REQ):
                LOG(INFO) << "";
                //responseMsg =  createMsgToken(BcdID *bcd_id_p, msg_type_t type, char *msg_payload, bytes_t msg_payload_len, uint8_t padding) {
				responseCode = htonl(REPLY_SUCCESS);
                addMsgToQueue(&bcd_id_val, MSG_INT_RPL, (char*)&responseCode, sizeof(int), 0, ctx);
                //responseMsg = createMsgToken(&bcd_id_val, MSG_INT_RPL, msg, strlen(msg), 0);
                //llring_enqueue(queue_, responseMsg);

                break;
            case (MSG_WRT_REQ):
                {
                    LOG(INFO) << "Received MSG_WRT_REQ";
                    char fullPath[FILENAME_LEN] = {0};
                    BcdIdtoFilename(&bcd_id_val, fullPath);
                    LOG(INFO) << "Going to use path of: " << fullPath;
					responseCode = htonl(REPLY_SUCCESS);
					numberOfWrts++;
					addMsgToQueue(&bcd_id_val, MSG_WRT_RPL, (char*)&responseCode, sizeof(int), 0, ctx);
                    char* file_path = (char*)malloc(strlen(fullPath) + 1);
                    strcpy(file_path, fullPath);
                    //llring_enqueue(file_queue_, file_path);
                    SendToFileGate((char*)&bcd_id_val, sizeof(BcdID), ctx);
                }
                break;
			case (MSG_READ_REQ):
				{
					LOG(INFO) << "Recveived MSG_READ_REQ";
					//Check if we've already receive this file
					//For testing, assume we have.
					if(1==1){
						responseCode = htonl(REPLY_SUCCESS);
						char fullPath[FILENAME_LEN] = {0};
						BcdIdtoFilename(&bcd_id_val, fullPath);
						FILE* fp = fopen(fullPath, "r");
						if(fp == NULL){
							LOG(INFO) << "File: " << fullPath << " failed to open";
							LOG(ERROR) << "File: " << fullPath << " failed to open";
							return;
						}
						fseek(fp, 0L, SEEK_END);
						uint64_t filesize = ftell(fp);
						uint64_t filesize_half = filesize >> 1;
						LOG(INFO) << "Filename: \"" << fullPath << "\" size: " << filesize << " filesize_half: " << filesize_half;
						fclose(fp);
						MsgReadRpl mr = { .code = htonl(REPLY_SUCCESS), .padding = 0, .start = htobe64(0), .jump = htobe64(filesize_half), .file_size = htobe64(filesize) };
						addMsgToQueue(&bcd_id_val, MSG_READ_RPL, (char*)&mr, sizeof(MsgReadRpl), 0, ctx);
					}
				}
				break;
			case (MSG_DEL_REQ):
				{
                    char fullPath[FILENAME_LEN] = {0};
                    BcdIdtoFilename(&bcd_id_val, fullPath);
                    LOG(INFO) << "Going to delete file: \"" << fullPath << "\"";
                    if(remove(fullPath) != 0){
                        LOG(INFO) << "Failed to remove specified file!";
                    }
                    MsgDelRpl mr = { .code = htonl(REPLY_SUCCESS) };
                    addMsgToQueue(&bcd_id_val, MSG_DEL_RPL, (char*)&mr, sizeof(MsgDelRpl), 0, ctx);
				}
				break;
            default:
                LOG(INFO) << "Received message of unknown type. ID: " << msgHdr_dat.type_dat.value();
        }
        //char pathBuf[PATH_MAX+1];

        //if(totalLength > PATH_MAX){
        //    LOG(INFO) << "Error, path too long. Length: " << totalLength;
        //    // bess::Free(pkt);
        //    continue;
        //}
        //else
        //{
        //    LOG(INFO) << "Total length valid: " << totalLength;
        //}

        //strncpy(pathBuf, ptr, totalLength);
        //pathBuf[totalLength] = '\0';//Should be fixed -- no need to copy here.

        // strcpy(sharedPath_, pathBuf);


        //LOG(INFO) << "Packet contains: " << pathBuf;

        //Now, read that file out
        //if(strlen(pathBuf) == 0){
        //    LOG(INFO) << "Invalid 0 length path.";
        //    continue;
        //}

        //Dynamic array

        //char *temporaryPathPtr = new char[totalLength+1];//Do this at initialization, don't dynamically allocate
        //strcpy(temporaryPathPtr, pathBuf);
    }

    //if(messagesSentCount == 0)
    //{
    //}
}

//struct task_result SparkInterface::RunTask(
//  Context *ctx,
//  bess::PacketBatch *batch,
//  __attribute__((unused)) void *arg) {
////
////    static int lastOpenFd;
////    static bool workingOnFd = false;
////
//    struct task_result taskResultVal;
//    taskResultVal.packets = 0u;
//    taskResultVal.bits = 0;
//    taskResultVal.block = true;
//
//    llring_addr_t ringBufObj;
//    int pktSentCnt = 0;
//    if(llring_dequeue(queue_, &ringBufObj) == 0) {
//        //LOG(INFO) << "Got item out of queue";
//        MsgToken* fromQueue = (MsgToken*)ringBufObj;
//        //LOG(INFO) << "Got fromQueue: " << HexDump(fromQueue->buf_p, fromQueue->msg_len);
//        //bess::Packet *newPkt = current_worker.packet_pool()->Alloc();
//        int i = 0;
//
//        char *newPtr = static_cast<char *>(batch->pkts()[i]->buffer()) + SNBUF_HEADROOM;
//
//        //Now we have to actually emit this data back to spark!
//        //memcpy(newPtr, memoryBuf, readSz);//Read directly into the packet -- don't copy
//        memcpy(newPtr, fromQueue->buf_p, fromQueue->msg_len);
//
//        batch->pkts()[i]->set_data_len(fromQueue->msg_len);
//        batch->pkts()[i]->set_total_len(fromQueue->msg_len);
//
//        //batch->add(newPkt);
//        pktSentCnt++;
//
//        RunNextModule(ctx, batch);
//
//        taskResultVal.packets = pktSentCnt;
//        taskResultVal.bits = 8*pktSentCnt;
//        taskResultVal.block = false;
//
//
//
//        
//        //LOG(INFO) << "Got: \"" << fromQueue->pkt << "\" len: " << fromQueue->len;
//    }
//    //else if(llring_dequeue(file_queue_, &ringBufObj) == 0) {
//    //    LOG(INFO) << "=========================================";
//    //    LOG(INFO) << "Got something off the file_queue: \"" << (char*)ringBufObj;
//    //    LOG(INFO) << "=========================================";
//
//    //    bess::Packet *newPkt = current_worker.packet_pool()->Alloc();
//    //    char *newPtr = static_cast<char *>(newPkt->buffer()) + SNBUF_HEADROOM;
//    //    memcpy(newPtr, ringBufObj, strlen((char*)ringBufObj));
//
//    //    EmitPacket(ctx, newPkt, file_gate_);
//    //    free(ringBufObj);
//
//    //    pktSentCnt++;
//    //    taskResultVal.packets = pktSentCnt;
//    //    taskResultVal.bits = 8*pktSentCnt;
//    //    taskResultVal.block = false;
//    //}
//
////
////
////    llring_addr_t ringBufObj;
////    bool workToDo = false;
////
////    static int totalSentPackets = 0;
////
////    if(workingOnFd){//In progress
////        workToDo = true;
////    }
////    else if(llring_dequeue(queue_,  &ringBufObj) == 0){
////        LOG(INFO) << "Got path from the queue!!!: " << (char*)ringBufObj;
////
////        if((lastOpenFd = open((char*)ringBufObj, O_RDONLY)) == -1){
////            LOG(INFO) << "Failed to open file \"" << (char*)ringBufObj << "\"";
////            workToDo = false;
////        }
////        else
////        {
////            workToDo = true;
////        }
////        LOG(INFO) << "Going to delete: \"" << (char*)ringBufObj << "\" " << (void*)ringBufObj;
////        delete (char*)ringBufObj;
////    }
////
////
////    // llring_addr_t ringBufObj;
////    // int dequeueRes = llring_dequeue(queue_,  ringBufObj);
////    if(workToDo){
////        // int lastOpenFd;
////
////        // if((lastOpenFd = open(local_working_path, O_RDONLY)) == -1){
////        //     LOG(INFO) << "Failed to open file \"" << local_working_path << "\"";
////        //     goto exitEmpty;
////        // }
////
////        uint8_t memoryBuf[MAX_TOTAL_PACKET_SIZE];
////        ssize_t readSz = -1; 
////
////        unsigned int pktSentCnt = 0u;
////
////        // lseek(lastOpenFd, last_path_offset, SEEK_SET);
////        while(/*(readSz != 0) && */(! batch->full()) && (readSz = read(lastOpenFd, memoryBuf, MAX_TOTAL_PACKET_SIZE - h_size_)) > 0){
////            //Allocate that packet and process it.
////            // last_path_offset += readSz;
////            bess::Packet *newPkt = current_worker.packet_pool()->Alloc();
////
////            // LOG(INFO) << "Allocated pkt: " << newPkt;
////            // buf.add(pkt);
////            char *newPtr = static_cast<char *>(newPkt->buffer()) + SNBUF_HEADROOM + h_size_;
////
////            memcpy(newPtr, memoryBuf, readSz);//Read directly into the packet -- don't copy
////
////
////            newPkt->set_data_len(readSz);
////            newPkt->set_total_len(readSz);
////
////            batch->add(newPkt);
////            // RunNextModule(ctx, batch);
////            pktSentCnt++;
////        }
////        totalSentPackets += pktSentCnt;
////        if (readSz == 0) {
////            // LOG(INFO) << "readSz == 0";
////            // last_path_offset = 0;
////            LOG(INFO) << "Total packets sent: " << totalSentPackets;
////            totalSentPackets = 0;
////            workingOnFd = false;
////            if(close(lastOpenFd) != 0){
////                LOG(INFO) << "Error could not close fd " << lastOpenFd;
////            }
////        }
////        // LOG(INFO) << "Packet send cnt: " << pktSentCnt;
////
////        if(pktSentCnt > 0){
////            RunNextModule(ctx, batch);
////        }
////
////        taskResultVal.packets = pktSentCnt;
////        taskResultVal.bits = 8*pktSentCnt;
////        taskResultVal.block = false;
////
////        
////        return taskResultVal;
////    }
////
////
////    // LOG(INFO) << "Task Result arg: " << arg << ctx << batch;
//    return taskResultVal;
//}





// basically rte_hexdump() from eal_common_hexdump.c
static std::string HexDump(const void *buffer, size_t len) {
  std::ostringstream dump;
  size_t i, ofs;
  const char *data = reinterpret_cast<const char *>(buffer);

  dump << "Dump data at [" << buffer << "], len=" << len << std::endl;
  ofs = 0;
  while (ofs < len) {
    dump << std::setfill('0') << std::setw(8) << std::hex << ofs << ":";
    for (i = 0; ((ofs + i) < len) && (i < 16); i++) {
      dump << " " << std::setfill('0') << std::setw(2) << std::hex
           << (data[ofs + i] & 0xFF);
    }
    for (; i <= 16; i++) {
      dump << " | ";
    }
    for (i = 0; (ofs < len) && (i < 16); i++, ofs++) {
      char c = data[ofs];
      if ((c < ' ') || (c > '~')) {
        c = '.';
      }
      dump << c;
    }
    dump << std::endl;
  }
  return dump.str();
}


ADD_MODULE(SparkInterface, "spark interface", "interface for commubication with spark")
