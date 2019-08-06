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
    size_bcd_struct hdr;
} filewritedata_t;

/* from upstream */
void FileWriter::ProcessBatch(Context *ctx, bess::PacketBatch *batch)
{
  int cnt = batch->cnt();

  static filewritedata_t fwData = {false, 0, 0, NULL, {0,{{0,0},0}}};
  static int totalPktCnt = 0;

  for (int i = 0; i < cnt; i++)
  {

    bess::Packet *pkt = batch->pkts()[i];
    uint8_t *pkt_data_p = pkt->head_data<uint8_t*>(0) + sizeof(uint64_t) + sizeof(BcdID);
    //LOG(INFO) << "GOT PACKET len: " << pkt->total_len() << " data: " << pkt->Dump();

    if((pkt->total_len() > (int64_t)(sizeof(uint64_t) + sizeof(BcdID) + h_size_))){
        static size_bcd_struct hdr;
        totalPktCnt++;
        
        //bess::utils::Copy(pkt->head_data<uint8_t*>(0), &(hdr.filesize), sizeof(uint64_t));
        hdr.filesize = *((uint64_t*)pkt->head_data(0));
        hdr.bcd_id_val = *((BcdID*)pkt->head_data(sizeof(uint64_t)));

        //Only run when we have a new file in 1 of 2 cases
        if((fwData.currentlyWorking && ! cmpBcd(&(hdr.bcd_id_val), &(fwData.hdr.bcd_id_val)))
            || ! fwData.currentlyWorking){
            if(fwData.currentlyWorking){
                LOG(INFO) << "ERROR: we're actually reading a new file";
                LOG(INFO) << "We've only read " << fwData.writtenBytes << " of " << fwData.fileTotalBytes;
                LOG(INFO) << "cmpBcd: " << cmpBcd(&(hdr.bcd_id_val), &(fwData.hdr.bcd_id_val));
            }
            if(fwData.fp != NULL){
                fclose(fwData.fp);
            }
            fwData = {false, 0, 0, NULL, {0,{{0,0},0}}};

            LOG(INFO) << "Got filesize of: " << hdr.filesize;
            char fullPath[PATH_MAX];
            char dirName[PATH_MAX];

            BcdIdtoFilename( write_path_.c_str(), &(hdr.bcd_id_val), fullPath);
            strcpy(dirName, fullPath);
            dirname(dirName);
            LOG(INFO) << "Got path of: " << fullPath;
            LOG(INFO) << "Dirname of: " << dirName;

            struct stat sb;

            if (stat(dirName, &sb) == -1) {
              //If we fail to stat then create the directory
              //mkdir(dirName, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
              LOG(INFO) << "mkdir " << dirName << " result: " << mkdir(dirName, 0777);
            }
            if((fwData.fp = fopen(fullPath, "w")) == NULL){
              LOG(ERROR) << "FileWriter error opening path: " << fullPath;
            }
            else {
                fwData.currentlyWorking = true;
                fwData.fileTotalBytes = hdr.filesize;
                fwData.writtenBytes = 0;
                fwData.hdr = hdr;
            }
        }
    }
    if(fwData.currentlyWorking){
        fwData.writtenBytes += fwrite(pkt_data_p, sizeof(char), pkt->total_len() - (sizeof(uint64_t) + sizeof(BcdID) + h_size_), fwData.fp);
        if(fwData.writtenBytes >= fwData.fileTotalBytes){
            fclose(fwData.fp);
            LOG(INFO) << "Received totalPktCnt: " << totalPktCnt;
            fwData = {false, 0, 0, NULL, {0,{{0,0},0}}};
        }
    }
    

    bess::Packet::Free(pkt);
  }
}

ADD_MODULE(FileWriter, "file_writer",
           "description of file_writer")
