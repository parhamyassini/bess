// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "file_writer.h"
#include <bitset>
#include <cstdlib>

#include "spark_common.h"

#include "pb/file_writer_msg.pb.h"
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>

#define DEFAULT_BUFFEREDQUEUE_SIZE 1024

CommandResponse FileWriter::Init(const sample::file_writer::pb::FileWriterArg &arg)
{
  write_path_ = arg.write_path();
  LOG(INFO) << "Configured to use path: \"" << write_path_.c_str() << "\"";
  return CommandSuccess();
}

typedef struct _filewritedata {
    bool currentlyWorking;
    off_t writtenBytes;
    off_t fileTotalBytes;
    FILE* fp;
} filewritedata_t;

/* from upstream */
void FileWriter::ProcessBatch(Context *ctx, bess::PacketBatch *batch)
{
  int cnt = batch->cnt();

  static filewritedata_t fwData = {false, 0, 0, NULL};

  for (int i = 0; i < cnt; i++)
  {

    bess::Packet *pkt = batch->pkts()[i];
    uint8_t *pkt_data_p = pkt->head_data<uint8_t*>(0);
    LOG(INFO) << "GOT PACKET len: " << pkt->total_len() << " data: " << pkt->Dump();

    //LOG(INFO) << "Got total packet length for " << i << " of " << pkt->total_len() << " tot: " << sizeof(uint64_t) + sizeof(BcdID);
    if(fwData.currentlyWorking == 0 && pkt->total_len() < ((int32_t)sizeof(uint64_t) + (int32_t)sizeof(BcdID))){
        LOG(INFO) << "Skipping this packet";
        continue;
    }
    else if(fwData.currentlyWorking == false){
      LOG(INFO) << "Got first packet!";

      //msgHdr_t msgHdr_dat;
      //parseMsgHdr(&msgHdr_dat, pkt_data_p);
      //
      uint64_t filesize = *((uint64_t*)pkt_data_p);
      //LOG(INFO) << "Got filesize of: " << filesize;

      //LOG(INFO) << pkt->Dump();

      BcdID* bcd_id_val = ((BcdID*)(pkt_data_p + sizeof(uint64_t)));
      LOG(INFO) << "Got bcd_id, msb: " << std::hex << bcd_id_val->app_id.msb << " lsb: " << std::hex << bcd_id_val->app_id.lsb << " dataid: " << std::hex << bcd_id_val->data_id;
      //bcd_id_val.app_id  = {msgHdr_dat.msb_dat.value(), msgHdr_dat.lsb_dat.value()};
      //bcd_id_val.data_id = msgHdr_dat.id_dat.value();

      char fullPath[FILENAME_LEN] = {0};
      char dirName[FILENAME_LEN] = {0};
      BcdIdtoFilename(write_path_.c_str(), bcd_id_val, fullPath);
      strcpy(dirName, fullPath);
      dirname(dirName);

      LOG(INFO) << "FileWriter got path of: " << fullPath;
      LOG(INFO) << "FileWriter got dirName of: " << dirName;

      //Check that the directory has been created, create it otherwise
      struct stat sb;

      if (stat(dirName, &sb) == -1) {
        //If we fail to stat then create the directory
        //mkdir(dirName, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        mkdir(dirName, 0777);
      }
      
      if((fwData.fp = fopen(fullPath, "w")) == NULL){
        LOG(ERROR) << "FileWriter error opening path: " << fullPath;
        LOG(INFO) << "FileWriter error opening path: " << fullPath;
      }
      
      fwData.currentlyWorking = true;
      fwData.fileTotalBytes = filesize;
    }
    else if (fwData.currentlyWorking == true && (fwData.fileTotalBytes > fwData.writtenBytes)){
      LOG(INFO) << "IN THE FILE WRITER!!!!";
      size_t sizeToWrite = pkt->total_len();
      int realSizeWritten;
      LOG(INFO) << "FileWriter writing: " << sizeToWrite << " bytes";
      if((realSizeWritten = fwrite(pkt_data_p, sizeof(uint8_t), sizeToWrite, fwData.fp) != sizeToWrite)){
        LOG(INFO) << "Write is short by: " << sizeToWrite - realSizeWritten << " bytes";
      }
      fwData.writtenBytes += realSizeWritten;
      LOG(INFO) << "We've written: " << fwData.writtenBytes;
      //if(fwData.writtenBytes == fwData.fileTotalBytes){
      //  fwData.currentlyWorking = false;
      //  fwData.fp = NULL;
      //}
    }


    


    //Ethernet *eth = pkt->head_data<Ethernet *>(); // Ethernet
    //Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1); // IP
    //int ip_bytes = ip->header_length << 2;

    // Access UDP payload (i.e., mDC data)
    //uint8_t offset = sizeof(Ethernet) + ip_bytes + sizeof(Udp);

    //FILE *fd_d = get_attr<FILE *>(this, ATTR_R_FILE_D, pkt);
    //uint8_t header_size_ = get_attr<uint8_t>(this, ATTR_R_HEADER_SIZE, pkt);
    //uint8_t data_size_ = get_attr<uint8_t>(this, ATTR_R_DATA_SIZE, pkt);

    //be64_t *mdc_p1 = pkt->head_data<be64_t *>(offset);     // first 8 bytes
    //be64_t *mdc_p2 = pkt->head_data<be64_t *>(offset + 8); // second 8 bytes

    //uint16_t addr = (mdc_p1->raw_value() & 0xffff);
    //uint8_t mode = (mdc_p1->raw_value() & 0xff0000) >> 16;
    //uint8_t label = (mdc_p1->raw_value() & 0xff000000) >> 24;
    //uint8_t code = (mdc_p1->raw_value() & 0xff00000000) >> 32;
    //uint8_t app_id = (mdc_p1->raw_value() & 0xff0000000000) >> 40;
    //uint8_t data_id = (mdc_p1->raw_value() & 0xff000000000000) >> 48;
    //uint8_t sn = (mdc_p1->raw_value() & 0xff00000000000000) >> 56;
    //uint8_t data_size = (mdc_p2->raw_value() & 0xff);
    //std::cout << "FileWriter ProcessBatch batch size: " + std::to_string(cnt) + " pkt: " + std::to_string(i) << std::endl;
    //std::cout << std::hex << addr << std::endl;
    //std::cout << std::hex << static_cast<int>(mode) << std::endl;
    //std::cout << std::hex << static_cast<int>(label) << std::endl;
    //std::cout << std::hex << static_cast<int>(code) << std::endl;
    //std::cout << std::hex << static_cast<int>(app_id) << std::endl;
    //std::cout << std::hex << static_cast<int>(data_id) << std::endl;
    //std::cout << std::hex << std::to_string(sn) << std::endl;
    //std::cout << std::hex << std::to_string(data_size) << std::endl;

    // fwrite to fd_d
    //uint8_t data_written = fwrite(pkt->head_data<void *>(offset + header_size_), sizeof(char), data_size_, fd_d);
    //fclose(fd_d);

    //// Reply with data id, amount of data written,
    //// Rewrite mode field to 0x14
    //char *head = pkt->head_data<char *>(offset);
    //uint8_t *code_p = reinterpret_cast<uint8_t *>(head + 4);
    //uint8_t *data_size_p = reinterpret_cast<uint8_t *>(head + 8);
    //*code_p = (*code_p & 0x00) | 0x06;
    //*data_size_p = (*data_size_p & 0x00) | data_written;

    //be64_t *mdc_p1_ = pkt->head_data<be64_t *>(offset);     // first 8 bytes
    //be64_t *mdc_p2_ = pkt->head_data<be64_t *>(offset + 8); // second 8 bytes

    //uint8_t code_ = (mdc_p1_->raw_value() & 0xff00000000) >> 32;
    //uint8_t data_size_1 = (mdc_p2_->raw_value() & 0xff);

    //std::cout << "FileWriter send reply: " << std::endl;
    //std::cout << std::hex << static_cast<int>(code_) << std::endl;
    //std::cout << std::hex << std::to_string(data_size_1) << std::endl;

    //EmitPacket(ctx, pkt, 0);//The new version will not be sending any form of ACK
  }
}

ADD_MODULE(FileWriter, "file_writer",
           "description of file_writer")
