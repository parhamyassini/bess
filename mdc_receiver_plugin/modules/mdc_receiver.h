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

#include "pb/mdc_receiver_msg.pb.h"


//using bess::utils::ExactMatchField;
//using bess::utils::ExactMatchKey;
//using bess::utils::ExactMatchRuleFields;
//using bess::utils::ExactMatchTable;
using bess::utils::Error;

using bess::utils::be16_t;
using bess::utils::be32_t;
using bess::utils::Ethernet;
using bess::utils::Mdc;

//static const size_t kMaxVariable = 16;

#define MDC_MAX_TABLE_SIZE (1048576 * 64)
#define MDC_DEFAULT_TABLE_SIZE 1024
#define MDC_MAX_BUCKET_SIZE 8

typedef uint64_t mac_addr_t;
typedef uint32_t mdc_label_t;

struct alignas(32) mdc_entry {
    union {
        struct {
            uint64_t addr : 48;
            uint64_t label : 24;
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

  MdcReceiver() : Module(), mdc_table_() {}

  CommandResponse Init(const sample::mdc_receiver::pb::MdcReceiverArg &arg);
  void DeInit();

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const sample::mdc_receiver::pb::MdcReceiverArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

private:
    struct mdc_table mdc_table_;
};

#endif // BESS_MODULES_LABELLOOKUP_H_
