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

#include "aws_mdc_dummy_switch.h"


const Commands AwsMdcDummySwitch::cmds = {
        {"clear", "EmptyArg", MODULE_CMD_FUNC(&AwsMdcDummySwitch::CommandClear), Command::THREAD_SAFE},
};

CommandResponse
AwsMdcDummySwitch::Init(const sample::aws_mdc_dummy_switch::pb::AwsMdcDummySwitchArg &arg) {

    switch_id_ = arg.switch_id();
    timestamp_en_ = arg.enable_timestamp();

    uint64_t latency_ns_max = kDefaultMaxNs;
    uint64_t latency_ns_resolution = kDefaultNsPerBucket;
    uint64_t quotient = latency_ns_max / latency_ns_resolution;
    if ((latency_ns_max % latency_ns_resolution) != 0) {
        quotient += 1;  // absorb any remainder
    }
    if (quotient > rtt_hist_.max_num_buckets() / 2) {
        return CommandFailure(E2BIG, "excessive latency_ns_max / latency_ns_resolution");
    }

    size_t num_buckets = quotient;
    rtt_hist_.Resize(num_buckets, latency_ns_resolution);

    if (arg.switch_ips_size() <= 0 || arg.switch_ips_size() > 16) {
        return CommandFailure(EINVAL, "no less than 1 and no more than 16 switch IPs");
    }
    if (arg.switch_macs_size() <= 0 || arg.switch_macs_size() > 16) {
        return CommandFailure(EINVAL, "no less than 1 and no more than 16 switch MACs");
    }

    if (arg.agents_ips_size() <= 0 || arg.agents_ips_size() > 16) {
        return CommandFailure(EINVAL, "no less than 1 and no more than 16 agent IPs");
    }
    if (arg.agents_macs_size() <= 0 || arg.agents_macs_size() > 16) {
        return CommandFailure(EINVAL, "no less than 1 and no more than 16 agent MACs");
    }

    if ((arg.agents_macs_size() != arg.agents_ips_size()) ||
        (arg.switch_macs_size() != arg.switch_ips_size()) ||
        (arg.agents_macs_size() != arg.switch_macs_size())) {
        return CommandFailure(EINVAL, "switch needs to have same number of agent and switch IPs and MACs");
    }

    for (int i = 0; i < arg.switch_ips_size(); i++) {
        const std::string &switch_mac_str = arg.switch_macs(i);
        const std::string &agent_mac_str = arg.agents_macs(i);
        const std::string &switch_ip_str = arg.switch_ips(i);
        const std::string &agent_ip_str = arg.agents_ips(i);

        switch_macs_[i] = Ethernet::Address(switch_mac_str);
        agent_macs_[i] = Ethernet::Address(agent_mac_str);

        be32_t switch_ip, agent_ip;
        bool ip_parsed = bess::utils::ParseIpv4Address(switch_ip_str, &switch_ip);
        if (!ip_parsed) {
            return CommandFailure(EINVAL, "cannot parse switch IP %s", switch_ip_str.c_str());
        }

        ip_parsed = bess::utils::ParseIpv4Address(agent_ip_str, &agent_ip);
        if (!ip_parsed) {
            return CommandFailure(EINVAL, "cannot parse agent IP %s", agent_ip_str.c_str());
        }

        switch_ips_[i] = switch_ip;
        agent_ips_[i] = agent_ip;
    }

    return CommandSuccess();
}

void AwsMdcDummySwitch::DeInit() {
}


CommandResponse
AwsMdcDummySwitch::CommandClear(const bess::pb::EmptyArg &) {
    return CommandSuccess();
}

void
AwsMdcDummySwitch::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
    int cnt = batch->cnt();
    uint64_t now_ns = tsc_to_ns(rdtsc());

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

        int ip_bytes = ip->header_length << 2;
        // Access UDP payload (i.e., mDC data)
        be64_t *p = pkt->head_data<be64_t *>(sizeof(Ethernet) + ip_bytes + sizeof(Udp));
        uint64_t *ts = reinterpret_cast<uint64_t *>(p + 1);

        if (timestamp_en_) {
            if (*ts) {
                // Pkt received
                uint64_t diff;
                if (now_ns >= *ts && *ts != 0) {
                    diff = now_ns - *ts;
                    rtt_hist_.Insert(diff);
                    // a hack to collect results
                    if (first_pkt_fwd_ns_ && (now_ns - first_pkt_fwd_ns_) >= 60'000'000'000) {
                        timestamp_en_ = false;
                        std::vector<double> latency_percentiles{95, 99, 99.9, 99.99};
                        const auto &rtt = rtt_hist_.Summarize(latency_percentiles);
                        std::cout << rtt.avg;
                        std::cout << rtt.min;
                        std::cout << rtt.max;
                        for (int percentile_value : rtt.percentile_values)
                            std::cout << percentile_value;
                    }
                }
                DropPacket(ctx, pkt);
            } else {
                // stamp the pkt, and send it to active agent
                *ts = now_ns;

                if (unlikely(!first_pkt_fwd_ns_)) {
                    first_pkt_fwd_ns_ = now_ns;
                }

                Udp *udp = reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);
                eth->dst_addr = agent_macs_[active_agent_id_];
                ip->dst = agent_ips_[active_agent_id_];

                eth->src_addr = switch_macs_[active_agent_id_];
                ip->src = switch_ips_[active_agent_id_];
                ip->id = be16_t(active_agent_id_ + 1);
                ip->fragment_offset = be16_t(Ipv4::Flag::kDF);
                udp->checksum = CalculateIpv4UdpChecksum(*ip, *udp);
                ip->checksum = bess::utils::CalculateIpv4Checksum(*ip);
                EmitPacket(ctx, pkt, active_agent_id_);
            }
        }
    }
}

ADD_MODULE(AwsMdcDummySwitch, "aws_mdc_dummy_switch", "Software mDC switch - used for latency measurements")
