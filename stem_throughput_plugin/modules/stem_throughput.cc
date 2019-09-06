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

#include "stem_throughput.h"


const Commands StemThroughput::cmds = {
        {"clear", "EmptyArg", MODULE_CMD_FUNC(&StemThroughput::CommandClear), Command::THREAD_UNSAFE},
};

CommandResponse
StemThroughput::Init(const sample::stem_throughput::pb::StemThroughputArg &arg) {
    prefix_ = arg.prefix();
    max_time_index_ = arg.max_time_index();
    return CommandSuccess();
}

void
StemThroughput::DeInit() {
}


CommandResponse
StemThroughput::CommandClear(const bess::pb::EmptyArg &) {
    return CommandSuccess();
}

void
StemThroughput::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
    int cnt = batch->cnt();
    uint64_t now_ns = tsc_to_ns(rdtsc());

    if (!enabled_) {
        RunNextModule(ctx, batch);
        return;
    }

    if(prev_ns_ == 0) {
        prev_ns_ = now_ns;
    }

    prev_pkt_cnt_ += cnt;
    tot_pkt_cnt_ += cnt;	

    for (int i = 0; i < cnt; i++) {
        const auto &len = batch->pkts()[i]->total_len();
	prev_bytes_cnt_ += len;
	tot_bytes_cnt_ += len;
    }

    // tot_pkt_cnt_ += cnt;
    // tot_bytes_cnt_ += prev_bytes_cnt_;

    uint64_t diff = now_ns - prev_ns_;
    if(diff >= kDefaultMaxNs) {
        time_idx_ += 1;
        prev_pkt_cnt_ = 0;
        prev_bytes_cnt_ = 0;
        prev_ns_ = now_ns;
    }

    if(time_idx_ >= max_time_index_) {
        std::cout << prefix_;
        std::cout << (tot_pkt_cnt_ / (1000000.0 * max_time_index_));
        std::cout << ((tot_bytes_cnt_ * 8.0) / (1000000000.0 * max_time_index_));
        enabled_ = false;
    }

    RunNextModule(ctx, batch);
}

ADD_MODULE(StemThroughput, "stem_throughput", "measures throughput of Stem pkts")
