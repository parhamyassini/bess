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

#include "aws_mdc_dummy_receiver.h"

const Commands AwsMdcDummyReceiver::cmds = {
        {"clear", "EmptyArg", MODULE_CMD_FUNC(&AwsMdcDummyReceiver::CommandClear), Command::THREAD_SAFE},
};

CommandResponse
AwsMdcDummyReceiver::Init(const sample::aws_mdc_dummy_receiver::pb::AwsMdcDummyReceiverArg &arg) {
    const std::string &switch_mac_str = arg.switch_mac();
    const std::string &agent_mac_str = arg.agent_mac();
    const std::string &switch_ip_str = arg.switch_ip();
    const std::string &agent_ip_str = arg.agent_ip();

    switch_mac_ = Ethernet::Address(switch_mac_str);
    agent_mac_ = Ethernet::Address(agent_mac_str);

    bool ip_parsed = bess::utils::ParseIpv4Address(switch_ip_str, &switch_ip_);
    if (!ip_parsed) {
        return CommandFailure(EINVAL, "cannot parse switch IP %s", switch_ip_str.c_str());
    }

    ip_parsed = bess::utils::ParseIpv4Address(agent_ip_str, &agent_ip_);
    if (!ip_parsed) {
        return CommandFailure(EINVAL, "cannot parse agent IP %s", agent_ip_str.c_str());
    }

    return CommandSuccess();
}

void
AwsMdcDummyReceiver::DeInit() {
}


CommandResponse
AwsMdcDummyReceiver::CommandClear(const bess::pb::EmptyArg &) {
    return CommandSuccess();
}

void
AwsMdcDummyReceiver::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
    int cnt = batch->cnt();
    for (int i = 0; i < cnt; i++) {
        bess::Packet *pkt = batch->pkts()[i];
        Ethernet *eth = pkt->head_data<Ethernet *>();

        // If it isn't an IP4 pkt
        if (eth->ether_type != be16_t(Ethernet::Type::kIpv4)) {
            DropPacket(ctx, pkt);
            continue;
        }

        Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

        // If it's not a UDP pkt
        if (ip->protocol != Ipv4::Proto::kUdp) {
            DropPacket(ctx, pkt);
            continue;
        }

        eth->dst_addr = switch_mac_;
        ip->dst = switch_ip_;

        eth->src_addr = agent_mac_;
        ip->src = agent_ip_;

        EmitPacket(ctx, pkt, 0);
    }

}

ADD_MODULE(AwsMdcDummyReceiver, "aws_mdc_dummy_receiver", "dummy mDC receiver in AWS hosts")
