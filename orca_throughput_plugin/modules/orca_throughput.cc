// Copyright (c) 2014-2017, The Regents of the University of California.
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

#include "orca_throughput.h"

const Commands OrcaThroughput::cmds = {
        {"clear", "EmptyArg", MODULE_CMD_FUNC(&OrcaThroughput::CommandClear), Command::THREAD_UNSAFE},
        {"get_latest", "OrcaThroughputCommandGetLatestArg", MODULE_CMD_FUNC(&OrcaThroughput::CommandGetLatest),
                                                                         Command::THREAD_SAFE}
};

/**
 * Returns the total time (in ns) spent for our lable and send algorithm. 
 * Param: clear (bool), if this param is set, it will clear the counter after the call and else the number will be acummulated
 */
CommandResponse OrcaThroughput::CommandGetLatest(const sample::orca_throughput::pb::OrcaThroughputCommandGetLatestArg &arg) 
{
    sample::orca_throughput::pb::OrcaThroughputCommandGetLatestResponse r;
    r.set_timestamp(tsc_to_ns(rdtsc()));
    r.set_bytes(prev_bytes_cnt_);
    r.set_packets(prev_pkt_cnt_);
    if(arg.clear() == 1) {
        mcslock_node_t mynode;
        mcs_lock(&lock_, &mynode);
        prev_bytes_cnt_ = 0;
        prev_pkt_cnt_ = 0;
        mcs_unlock(&lock_, &mynode);
    }
    return CommandSuccess(r);
}

CommandResponse
OrcaThroughput::Init(const bess::pb::EmptyArg &) {
    mcs_lock_init(&lock_);
    return CommandSuccess();
}

void
OrcaThroughput::DeInit() {
}


CommandResponse
OrcaThroughput::CommandClear(const bess::pb::EmptyArg &) {
    return CommandSuccess();
}

void
OrcaThroughput::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
    mcslock_node_t mynode;
    mcs_lock(&lock_, &mynode);
    
    int cnt = batch->cnt();

    prev_pkt_cnt_ += cnt;

    for (int i = 0; i < cnt; i++) {
        prev_bytes_cnt_ += (batch->pkts()[i]->total_len() + 24);
    }
    prev_ns_ = tsc_to_ns(rdtsc());
    
    mcs_unlock(&lock_, &mynode);
    RunNextModule(ctx, batch);
}

ADD_MODULE(OrcaThroughput, "orca_throughput", "measures rx pkt throughput at Orca agents")

