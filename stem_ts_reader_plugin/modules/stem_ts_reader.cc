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

#include "stem_ts_reader.h"


const Commands StemTSReader::cmds = {
        {"clear", "EmptyArg", MODULE_CMD_FUNC(&StemTSReader::CommandClear), Command::THREAD_SAFE},
};

CommandResponse
StemTSReader::Init(const sample::stem_ts_reader::pb::StemTSReaderArg &) {
    return CommandSuccess();
}

void StemTSReader::DeInit() {
}


CommandResponse
StemTSReader::CommandClear(const bess::pb::EmptyArg &) {
    return CommandSuccess();
}

void
StemTSReader::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
    int cnt = batch->cnt();
    uint64_t now_ns = tsc_to_ns(rdtsc());

    for (int i = 0; i < cnt; i++) {
        bess::Packet *pkt = batch->pkts()[i];
        // Access UDP payload (i.e., mDC data)
        be64_t *p = pkt->head_data<be64_t *>();
        uint64_t *ts = reinterpret_cast<uint64_t *>(p);

        if (unlikely(!first_pkt_rec_ns_)) {
            first_pkt_rec_ns_ = now_ns;
        }

        if (*ts) {
            // Pkt received
            uint64_t diff;
            if (now_ns >= *ts && *ts != 0) {
                diff = now_ns - *ts;
                rtt_hist_.Insert(diff);
                // a hack to collect results
                if (first_pkt_rec_ns_ && (now_ns - first_pkt_rec_ns_) >= 60'000'000'000) {
                    std::vector<double> latency_percentiles{50, 95, 99, 99.9, 99.99};
                    const auto &rtt = rtt_hist_.Summarize(latency_percentiles);
                    std::cout << rtt.avg;
                    std::cout << rtt.min;
                    std::cout << rtt.max;
                    for (int percentile_value : rtt.percentile_values)
                        std::cout << percentile_value;
                }
            }
        }
        DropPacket(ctx, pkt);
    }
}

ADD_MODULE(StemTSReader, "stem_ts_reader", "StemTSReader")
