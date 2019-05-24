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

#ifndef BESS_MODULES_LABELLOOKUP_H_
#define BESS_MODULES_LABELLOOKUP_H_

#include <map>

#include "module.h"
#include "utils/bits.h"
#include "utils/endian.h"
#include "utils/time.h"
#include "utils/checksum.h"
#include "utils/ether.h"
#include "utils/arp.h"
#include "utils/ip.h"
#include "utils/udp.h"
#include "utils/histogram.h"
#include "utils/exact_match_table.h"

#include "pb/stem_ts_reader_msg.pb.h"

using bess::utils::Error;

using bess::utils::be16_t;
using bess::utils::be32_t;
using bess::utils::be64_t;
using bess::utils::Ethernet;
using bess::utils::Arp;
using bess::utils::Ipv4;
using bess::utils::Udp;

class StemTSReader final : public Module {
public:
  static const Commands cmds;
  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

    StemTSReader() : Module(), first_pkt_rec_ns_(), rtt_hist_(0, 1) {
      max_allowed_workers_ = Worker::kMaxWorkers;
  }

  CommandResponse Init(const sample::stem_ts_reader::pb::StemTSReaderArg &arg);
  void DeInit();

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

private:
    static const uint64_t kDefaultNsPerBucket = 100;
    static const uint64_t kDefaultMaxNs = 10'000'000;  // 10 ms

    uint64_t first_pkt_rec_ns_;
    Histogram<uint64_t> rtt_hist_;
};

#endif // BESS_MODULES_LABELLOOKUP_H_
