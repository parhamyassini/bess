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


#include "module.h"
#include "utils/common.h"
#include "utils/bits.h"
#include "utils/endian.h"
#include "utils/ether.h"
#include "utils/time.h"
#include "../mdc_receiver_plugin/utils/mdc.h"

#include "pb/aws_mdc_pkt_gen_msg.pb.h"

#include <queue>


//using bess::utils::Error;

using bess::utils::be16_t;
using bess::utils::be32_t;
using bess::utils::Ethernet;
using bess::utils::Mdc;

#define MAX_TEMPLATE_SIZE 1536

typedef uint64_t Event;
typedef std::priority_queue<Event, std::vector<Event>, std::greater<Event>>
        EventQueue;


class AwsMdcPktGen final : public Module {
public:
    static const Commands cmds;
    static const gate_idx_t kNumIGates = 0;
    static const gate_idx_t kNumOGates = 1;

    AwsMdcPktGen()
            : Module(),
              events_(),
              tmpl_(),
              template_size_(),
              total_pps_(),
              burst_() {
        is_task_ = true;
    }

    CommandResponse Init(const sample::aws_mdc_pkt_gen::pb::AwsMdcPktGenArg &arg);

    void DeInit() override;

    struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
                               void *arg) override;

    CommandResponse ProcessUpdatableArgs(const sample::aws_mdc_pkt_gen::pb::AwsMdcPktGenArg &arg);

    CommandResponse CommandUpdate(const sample::aws_mdc_pkt_gen::pb::AwsMdcPktGenArg &arg);

    bess::Packet *FillMdcPacket();
    void GeneratePackets(Context *ctx, bess::PacketBatch *batch);

private:
    // Priority queue of future events
    EventQueue events_;

    unsigned char tmpl_[MAX_TEMPLATE_SIZE] = {};
    int template_size_;

    /* load parameters */
    double total_pps_;
    int burst_;

};

#endif // BESS_MODULES_LABELLOOKUP_H_
