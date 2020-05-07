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
#include "utils/bits.h"
#include "utils/endian.h"
#include "utils/ether.h"
#include "utils/mdc.h"
#include "utils/exact_match_table.h"
#include "utils/histogram.h"

#include "pb/mdc_receiver_msg.pb.h"


//using bess::utils::ExactMatchField;
//using bess::utils::ExactMatchKey;
//using bess::utils::ExactMatchRuleFields;
//using bess::utils::ExactMatchTable;
using bess::utils::Error;

using bess::utils::be16_t;
using bess::utils::be32_t;
using bess::utils::be64_t;
using bess::utils::Ethernet;
using bess::utils::Mdc;

//static const size_t kMaxVariable = 16;

#define MDC_MAX_TABLE_SIZE (1048576 * 64)
#define MDC_DEFAULT_TABLE_SIZE 1024
#define MDC_MAX_BUCKET_SIZE 8

#define MDC_OUTPUT_TOR 0
#define MDC_OUTPUT_INTERNAL 1
#define MDC_OUTPUT_HC 2

#define MDC_INPUT_APP 0 
#define MDC_INPUT_EXT 1

#define MDC_PKT_TYPE_MASK    0x00000000000000ff
#define MDC_PKT_AGENT_MASK   0x000000000000ff00
#define MDC_PKT_ADDRESS_MASK 0x00000000ffff0000
#define MDC_PKT_LABEL_MASK   0xffffffff00000000

#define MDC_PKT_IP_TYPE_MASK    0x000000ff00000000 // After reading the first 64 bits
#define MDC_PKT_IP_AGENT_MASK   0x0000ff0000000000 // After reading the first 64 bits
#define MDC_PKT_IP_ADDRESS_MASK 0xffff000000000000 // After reading the first 64 bits
#define MDC_PKT_IP_LABEL_MASK   0x00000000ffffffff // After reading the second 64 bits

typedef uint64_t mac_addr_t;
typedef uint32_t mdc_label_t;
typedef uint8_t mdc_mode_t;

enum MDCPacketType {
  MDC_TYPE_UNLABELED = 0x01,
  MDC_TYPE_LABELED = 0x02,
  MDC_TYPE_SET_ACTIVE_AGENT = 0x00,
  MDC_TYPE_PING = 0xF0,
  MDC_TYPE_PONG = 0xF1,
  MDC_TYPE_SYNC_STATE = 0xF2,
  MDC_TYPE_SYNC_STATE_DONE = 0xF3,
};

struct alignas(32) mdc_entry {
    union {
        struct {
            uint64_t addr : 48;
            uint64_t label : 32;
            uint64_t occupied : 1;
        };
        uint64_t entry;
    };
};

struct mdc_table {
    struct mdc_entry *table;
    uint64_t size;
    uint64_t size_power;
    uint64_t bucket;
    uint64_t count;
};

class MdcReceiver final : public Module {
public:
  static const Commands cmds;
  // Three Outputs: Port0_out direct, App gate (File writer)
  static const gate_idx_t kNumOGates = 2;
  // Three Inputs: App gate, ext_gate(port0) and packet_generator
  static const gate_idx_t kNumIGates = 3;

  MdcReceiver() : Module(), agent_id_(), agent_label_(), mdc_table_(),
                     switch_mac_(), agent_mac_(), ip_encap_(),
                     emit_ping_pkt_(true), gen_ping_pkts_count_(0),
                     p_latency_enabled_(true), p_latency_first_pkt_rec_ns_(), p_latency_rtt_hist_(0, 1){
      max_allowed_workers_ = Worker::kMaxWorkers;
  }

  CommandResponse Init(const sample::mdc_receiver::pb::MdcReceiverArg &arg);
  void DeInit();

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const sample::mdc_receiver::pb::MdcReceiverCommandAddArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

private:
  /* TODO @parham: agent id should be 8 bits? remove agent_label? */
  uint32_t agent_id_;
  uint32_t agent_label_;
  
  struct mdc_table mdc_table_;

  Ethernet::Address switch_mac_;
  Ethernet::Address agent_mac_;

  uint64_t total_frwrd_app_pkts = 0;
  uint64_t total_rec_pkts = 0;
  uint64_t total_tor_pkt = 0;
  bool ip_encap_;
  bool emit_ping_pkt_;
  uint64_t gen_ping_pkts_count_;

  bool p_latency_enabled_;
  uint64_t p_latency_first_pkt_rec_ns_;
  Histogram<uint64_t> p_latency_rtt_hist_;
  
  /* These are the functions that actually do the processing after dividing packets */
  void DoProcessAppBatch(Context *ctx, bess::PacketBatch *batch);
  void DoProcessExtBatch(Context *ctx, bess::PacketBatch *batch);
  void LabelAndSendPacket(Context *ctx, bess::Packet *pkt);
};

#endif // BESS_MODULES_LABELLOOKUP_H_
