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

#include "mdc_health_check.h"


const Commands MdcHealthCheck::cmds = {
        {"add",   "MdcHealthCheckArg",
                              MODULE_CMD_FUNC(&MdcHealthCheck::CommandAdd), Command::THREAD_UNSAFE},
        {"clear", "EmptyArg", MODULE_CMD_FUNC(&MdcHealthCheck::CommandClear),
                                                                         Command::THREAD_UNSAFE},
};

CommandResponse
MdcHealthCheck::Init(const sample::mdc_health_check::pb::MdcHealthCheckArg &) {
    return CommandSuccess();
}

void MdcHealthCheck::DeInit() {
}

CommandResponse MdcHealthCheck::CommandAdd(
        const sample::mdc_health_check::pb::MdcHealthCheckArg &) {
    return CommandSuccess();
}

CommandResponse MdcHealthCheck::CommandClear(const bess::pb::EmptyArg &) {
    return CommandSuccess();
}

void MdcHealthCheck::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
    int cnt = batch->cnt();

    for (int i = 0; i < cnt; i++) {
        bess::Packet *pkt = batch->pkts()[i];
        Ethernet *eth = pkt->head_data<Ethernet *>();

        if (eth->ether_type != be16_t(Mdc::kControlType)) {

        } else {
            // non-MDC packets are dropped
            DropPacket(ctx, pkt);
            continue;
        }
    }

    RunNextModule(ctx, batch);
}

ADD_MODULE(MdcHealthCheck, "mdc_health_check",
           "ping-pong between MDC Agent and MDC router")
