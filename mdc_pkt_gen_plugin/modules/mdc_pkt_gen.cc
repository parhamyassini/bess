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

#include "mdc_pkt_gen.h"


const Commands MdcPktGen::cmds = {
        {"update", "MdcPktGenArg", MODULE_CMD_FUNC(&MdcPktGen::CommandUpdate),
                Command::THREAD_UNSAFE}
};

CommandResponse
MdcPktGen::CommandUpdate(const sample::mdc_pkt_gen::pb::MdcPktGenArg &arg) {
    return ProcessUpdatableArgs(arg);
}

CommandResponse
MdcPktGen::ProcessUpdatableArgs(const sample::mdc_pkt_gen::pb::MdcPktGenArg &arg) {
    if (arg.template_().length() == 0) {
        if (strnlen(reinterpret_cast<const char *>(tmpl_), MAX_TEMPLATE_SIZE) == 0) {
            return CommandFailure(EINVAL, "must specify 'template'");
        }
    } else {
        // update template
        if (arg.template_().length() > MAX_TEMPLATE_SIZE) {
            return CommandFailure(EINVAL, "'template' is too big");
        }

        const char *tmpl = arg.template_().c_str();
        const Ethernet *eth = reinterpret_cast<const Ethernet *>(tmpl);
        if (eth->ether_type != be16_t(Mdc::kControlHealthType)) {
            return CommandFailure(EINVAL, "'template' is not MDC Health Check");
        }

        template_size_ = arg.template_().length();
        memset(tmpl_, 0, MAX_TEMPLATE_SIZE);
        bess::utils::Copy(tmpl_, tmpl, template_size_);
    }

    if (arg.pps() != 0) {
        if (std::isnan(arg.pps()) || arg.pps() < 0.0) {
            return CommandFailure(EINVAL, "invalid 'pps'");
        }
        total_pps_ = arg.pps();
    }

    return CommandSuccess();
}

CommandResponse
MdcPktGen::Init(const sample::mdc_pkt_gen::pb::MdcPktGenArg &arg) {

    task_id_t tid = RegisterTask(nullptr);
    if (tid == INVALID_TASK_ID)
        return CommandFailure(ENOMEM, "Task creation failed");

    total_pps_ = 1000.0;
    burst_ = bess::PacketBatch::kMaxBurst;

    CommandResponse err;
    err = ProcessUpdatableArgs(arg);
    if (err.error().code() != 0) {
        return err;
    }

    /* cannot use ctx.current_ns in the master thread... */
    uint64_t now_ns = rdtsc() / tsc_hz * 1e9;
    events_.emplace(now_ns);

    return CommandSuccess();
}

void MdcPktGen::DeInit() {
}

bess::Packet *MdcPktGen::FillMdcPacket() {
    bess::Packet *pkt;

    int size = template_size_;

    if (!(pkt = current_worker.packet_pool()->Alloc())) {
        return nullptr;
    }

    char *p = pkt->buffer<char *>() + SNBUF_HEADROOM;

    pkt->set_data_off(SNBUF_HEADROOM);
    pkt->set_total_len(size);
    pkt->set_data_len(size);
    bess::utils::Copy(p, tmpl_, size, true);

    return pkt;
}


void MdcPktGen::GeneratePackets(Context *ctx, bess::PacketBatch *batch) {
    uint64_t now = ctx->current_ns;

    batch->clear();
    const int burst = ACCESS_ONCE(burst_);

    while (batch->cnt() < burst && !events_.empty()) {
        uint64_t t = events_.top();
        if (now < t)
            return;

        events_.pop();

        bess::Packet *pkt = FillMdcPacket();

        if (pkt) {
            batch->add(pkt);
        }

        events_.emplace(t + static_cast<uint64_t>(1e9 / total_pps_));
    }
}


struct task_result MdcPktGen::RunTask(Context *ctx, bess::PacketBatch *batch,
                                      void *) {
    if (children_overload_ > 0) {
        return {.block = true, .packets = 0, .bits = 0};
    }

    const int pkt_overhead = 24;

    GeneratePackets(ctx, batch);
    RunNextModule(ctx, batch);

    uint32_t cnt = batch->cnt();
    return {.block = (cnt == 0),
            .packets = cnt,
            .bits = ((template_size_ + pkt_overhead) * cnt) * 8};

}

ADD_MODULE(MdcPktGen, "mdc_pkt_gen",
           "generate MDC health-check pkts")
