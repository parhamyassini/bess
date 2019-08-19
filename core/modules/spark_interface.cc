#include "spark_interface.h"

//Alocates a packet, emits it, and frees
void SparkInterface::SendToFileGate(uint64_t filesize, char* data, int msg_len, Context *ctx){
    bess::Packet *newPkt = current_worker.packet_pool()->Alloc();

    char *newPtr = static_cast<char *>(newPkt->buffer()) + SNBUF_HEADROOM;
    //First, copy in the filesize
    uint64_t net_filesize = htobe64(filesize);
    memcpy(newPtr, &net_filesize, sizeof(uint64_t));

    //Now we have to actually emit this data back to spark!
    memcpy(newPtr + sizeof(uint64_t), data, msg_len);

    newPkt->set_data_len(msg_len);
    newPkt->set_total_len(msg_len);
    EmitPacket(ctx, newPkt, file_gate_);//Send the new packet out of the file gate
}



CommandResponse SparkInterface::Init(const bess::pb::SparkInterfaceArg &arg){
    //Get identifiers for spark_gate_ and file_gate_
    spark_gate_ = arg.spark_gate();
    file_gate_ = arg.file_gate();

    return CommandSuccess();
}

//Send a response message with given bcd_ip_p and type ID
uint8_t SparkInterface::addMsgToQueue(BcdID *bcd_id_p, msg_type_t type, char *msg_payload, bytes_t msg_payload_len, uint8_t padding, Context *ctx) {
    //create a message token
    MsgToken *mt_p = createMsgToken(bcd_id_p, type, msg_payload, msg_payload_len, padding);

    bess::Packet *newPkt = current_worker.packet_pool()->Alloc();//Allocate a new packet
    if(newPkt == NULL){
        return -1;
    }

    char *newPtr = static_cast<char *>(newPkt->buffer()) + SNBUF_HEADROOM;//Pointer to the data potion of the newly allocated packet
    memcpy(newPtr, mt_p->buf_p, mt_p->msg_len);//Copy the the message into the packet
    newPkt->set_data_len(mt_p->msg_len);//Set the correct lengths for the data
    newPkt->set_total_len(mt_p->msg_len);

    deleteMsgToken(mt_p);

    EmitPacket(ctx, newPkt,  spark_gate_);//Send the response packet to spark
    return 0;
}

void SparkInterface::ProcessBatch(__attribute__((unused)) Context *ctx, bess::PacketBatch *batch){
    //Process the packet to open the file, place that onto the queue
    for(int i = 0; i < batch->cnt(); i++){
        bess::Packet *pkt = batch->pkts()[i];
        uint8_t *ptr = pkt->head_data<uint8_t*>(0);//Create a pointer to the start of data in the packet

        msgHdr_t msgHdr_dat;
        parseMsgHdr(&msgHdr_dat, ptr);//Parse and save the contents of the packet into msgHdr_dat

        bess::Packet::Free(pkt);//Incoming packet freed


        BcdID bcd_id_val;//Create a bcd_id data structure
        bcd_id_val.app_id  = {msgHdr_dat.msb_dat.value(), msgHdr_dat.lsb_dat.value()};//Place the MSB and LSB into the struct
        bcd_id_val.data_id = msgHdr_dat.id_dat.value();//Place the data ID into the struct

        int responseCode;
        switch(msgHdr_dat.type_dat.value()){
            case (MSG_INT_REQ)://Perform initialization, this is an acknowledgement
                {
                    responseCode = htonl(REPLY_SUCCESS);
                    addMsgToQueue(&bcd_id_val, MSG_INT_RPL, (char*)&responseCode, sizeof(int), 0, ctx);
                }
                break;
            case (MSG_WRT_REQ)://Write request sent from spark
                {
                    LOG(INFO) << "Received MSG_WRT_REQ";

                    char fullPath[FILENAME_LEN] = {0};//Create a buffer for the path
                    BcdIdtoFilename( BCD_DIR_PREFIX, &bcd_id_val, fullPath );//Place the calculated filename into the fullPath buffer
                    responseCode = htonl(REPLY_SUCCESS);//Convert the reply code into network byte order
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
                        FILE* fp = NULL;
                        while(fp == NULL){
                            fp = fopen(fullPath, "r");
                            sleep(1);
                        }
                        //if(fp == NULL){
                        //    LOG(ERROR) << "File: " << fullPath << " failed to open";
                        //    return;
                        //}
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
