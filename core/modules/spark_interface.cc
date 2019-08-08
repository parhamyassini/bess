#include "spark_interface.h"

void SparkInterface::SendToFileGate(uint64_t filesize, char* data, int msg_len, Context *ctx){
    bess::Packet *newPkt = current_worker.packet_pool()->Alloc();

    char *newPtr = static_cast<char *>(newPkt->buffer()) + SNBUF_HEADROOM;
    //First, copy in the filesize
    uint64_t net_filesize = htobe64(filesize);
    memcpy(newPtr, &net_filesize, sizeof(uint64_t));

    //Now we have to actually emit this data back to spark!
    //memcpy(newPtr, memoryBuf, readSz);//Read directly into the packet -- don't copy
    memcpy(newPtr + sizeof(uint64_t), data, msg_len);

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

    for(int i = 0; i < batch->cnt(); i++){
        bess::Packet *pkt = batch->pkts()[i];
        uint8_t *ptr = pkt->head_data<uint8_t*>(0);

        msgHdr_t msgHdr_dat;
        parseMsgHdr(&msgHdr_dat, ptr);

        bess::Packet::Free(pkt);


        uint8_t buf[MAX_PKT_LEN - STD_HDR_LEN];
        memcpy(buf, ptr+STD_HDR_LEN, msgHdr_dat.len_dat.value() - STD_HDR_LEN);

        BcdID bcd_id_val;
        bcd_id_val.app_id  = {msgHdr_dat.msb_dat.value(), msgHdr_dat.lsb_dat.value()};
        bcd_id_val.data_id = msgHdr_dat.id_dat.value();

		static int numberOfWrts = 0;

		int responseCode;
        switch(msgHdr_dat.type_dat.value()){
            case (MSG_INT_REQ):
                LOG(INFO) << "";
				responseCode = htonl(REPLY_SUCCESS);
                addMsgToQueue(&bcd_id_val, MSG_INT_RPL, (char*)&responseCode, sizeof(int), 0, ctx);

                break;
            case (MSG_WRT_REQ):
                {
                    LOG(INFO) << "Received MSG_WRT_REQ";
                    char fullPath[FILENAME_LEN] = {0};
                    BcdIdtoFilename( BCD_DIR_PREFIX, &bcd_id_val, fullPath);
                    LOG(INFO) << "Going to use path of: " << fullPath;
					responseCode = htonl(REPLY_SUCCESS);
					numberOfWrts++;
					addMsgToQueue(&bcd_id_val, MSG_WRT_RPL, (char*)&responseCode, sizeof(int), 0, ctx);
                    //char* file_path = (char*)malloc(strlen(fullPath) + 1);
                    //strcpy(file_path, fullPath);
                    //llring_enqueue(file_queue_, file_path);
                    FILE* fp = fopen(fullPath, "r");
                    if(fp == NULL){
                        LOG(INFO) << "File: " << fullPath << " failed to open";
                        LOG(ERROR) << "File: " << fullPath << " failed to open";
                        return;
                    }
                    fseek(fp, 0L, SEEK_END);
                    uint64_t filesize = ftell(fp);
                    fclose(fp);
                    SendToFileGate(filesize, (char*)&bcd_id_val, sizeof(BcdID), ctx);
                    LOG(INFO) << "Msg Hdr Length: " << msgHdr_dat.len_dat;
                    LOG(INFO) << "Sent to file gate!";
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
						BcdIdtoFilename( BCD_DIR_PREFIX, &bcd_id_val, fullPath);
						FILE* fp = fopen(fullPath, "r");
						if(fp == NULL){
							LOG(ERROR) << "File: " << fullPath << " failed to open";
							return;
						}
						fseek(fp, 0L, SEEK_END);
						uint64_t filesize = ftell(fp);
						uint64_t filesize_half = filesize >> 1;
						fclose(fp);
						MsgReadRpl mr = { .code = htonl(REPLY_SUCCESS), .padding = 0, .start = htobe64(0), .jump = htobe64(filesize_half), .file_size = htobe64(filesize) };
						addMsgToQueue(&bcd_id_val, MSG_READ_RPL, (char*)&mr, sizeof(MsgReadRpl), 0, ctx);
					}
				}
				break;
			case (MSG_DEL_REQ):
				{
                    char fullPath[FILENAME_LEN] = {0};
                    BcdIdtoFilename( BCD_DIR_PREFIX, &bcd_id_val, fullPath);
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
    }
}


ADD_MODULE(SparkInterface, "spark interface", "interface for commubication with spark")
