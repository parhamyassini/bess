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

#include "aws_mdc_receiver.h"


static int is_power_of_2(uint64_t n) {
    return (n != 0 && ((n & (n - 1)) == 0));
}

static int aws_mdc_init(struct aws_mdc_table *mcd_tbl, int size, int bucket) {
    if (size <= 0 || size > AWS_MDC_MAX_TABLE_SIZE || !is_power_of_2(size)) {
        return -EINVAL;
    }

    if (bucket <= 0 || bucket > AWS_MDC_MAX_BUCKET_SIZE || !is_power_of_2(bucket)) {
        return -EINVAL;
    }

    if (mcd_tbl == nullptr) {
        return -EINVAL;
    }

    mcd_tbl->table = new(std::nothrow) aws_mdc_entry[size * bucket]{};

    if (mcd_tbl->table == nullptr) {
        return -ENOMEM;
    }

    mcd_tbl->size = size;
    mcd_tbl->bucket = bucket;

    /* calculates the log_2 (size) */
    mcd_tbl->size_power = 0;
    while (size > 1) {
        size = size >> 1;
        mcd_tbl->size_power += 1;
    }

    return 0;
}

static int aws_mdc_deinit(struct aws_mdc_table *mdc_tbl) {
    if (mdc_tbl == nullptr || mdc_tbl->table == nullptr || mdc_tbl->size == 0 ||
        mdc_tbl->bucket == 0) {
        return -EINVAL;
    }

    delete[] mdc_tbl->table;
    *mdc_tbl = {};
    return 0;
}

static uint32_t aws_mdc_ib_to_offset(struct aws_mdc_table *mdc_tbl, int index, int bucket) {
    return index * mdc_tbl->bucket + bucket;
}

static uint32_t aws_mdc_hash(mac_addr_t addr) {
    return rte_hash_crc_8byte(addr, 0);
}

static uint32_t aws_mdc_hash_to_index(uint32_t hash, uint32_t size) {
    return hash & (size - 1);
}

static uint32_t aws_mdc_alt_index(uint32_t hash, uint32_t size_power,
                                  uint32_t index) {
    uint64_t tag = (hash >> size_power) + 1;
    tag = tag * 0x5bd1e995;
    return (index ^ tag) & ((0x1lu << (size_power - 1)) - 1);
}

#if __AVX__
const union {
  uint64_t val[4];
  __m256d _mask;
} _mask = {.val = {0x8000ffffFFFFffffull, 0x8000ffffFFFFffffull,
                   0x8000ffffFFFFffffull, 0x8000ffffFFFFffffull}};

// Do not call these functions directly. Use find_index() instead. See below.
static inline int aws_find_index_avx(uint64_t addr, uint64_t *table) {
  DCHECK(reinterpret_cast<uintptr_t>(table) % 32 == 0);
  __m256d _addr = (__m256d)_mm256_set1_epi64x(addr | (1ull << 63));
  __m256d _table = _mm256_load_pd((double *)table);
  _table = _mm256_and_pd(_table, _mask._mask);
  __m256d cmp = _mm256_cmp_pd(_addr, _table, _CMP_EQ_OQ);

  return __builtin_ffs(_mm256_movemask_pd(cmp));
}
#else

static inline int aws_find_index_basic(uint64_t addr, uint64_t *table) {
    for (int i = 0; i < 4; i++) {
        if ((addr | (1ull << 63)) == (table[i] & 0x8000ffffFFFFffffull)) {
            return i + 1;
        }
    }

    return 0;
}

#endif

// Finds addr from a 4-way bucket *table and returns its index + 1.
// Returns zero if not found.
static inline int aws_find_index(uint64_t addr, uint64_t *table, const uint64_t) {
#if __AVX__
    return aws_find_index_avx(addr, table);
#else
    return aws_find_index_basic(addr, table);
#endif
}

static inline int aws_mdc_find(struct aws_mdc_table *mdc_tbl, uint64_t addr,
                               mdc_label_t *label) {
    size_t i;
    int ret = -ENOENT;
    uint32_t hash, idx1, offset;
    struct aws_mdc_entry *tbl = mdc_tbl->table;

    hash = aws_mdc_hash(addr);
    idx1 = aws_mdc_hash_to_index(hash, mdc_tbl->size);

    offset = aws_mdc_ib_to_offset(mdc_tbl, idx1, 0);

    if (mdc_tbl->bucket == 4) {
        int tmp1 = aws_find_index(addr, &tbl[offset].entry, mdc_tbl->count);
        if (tmp1) {
            *label = tbl[offset + tmp1 - 1].label;
            return 0;
        }

        idx1 = aws_mdc_alt_index(hash, mdc_tbl->size_power, idx1);
        offset = aws_mdc_ib_to_offset(mdc_tbl, idx1, 0);

        int tmp2 = aws_find_index(addr, &tbl[offset].entry, mdc_tbl->count);

        if (tmp2) {
            *label = tbl[offset + tmp2 - 1].label;
            return 0;
        }

    } else {
        /* search buckets for first index */
        for (i = 0; i < mdc_tbl->bucket; i++) {
            if (tbl[offset].occupied && addr == tbl[offset].addr) {
                *label = tbl[offset].label;
                return 0;
            }

            offset++;
        }

        idx1 = aws_mdc_alt_index(hash, mdc_tbl->size_power, idx1);
        offset = aws_mdc_ib_to_offset(mdc_tbl, idx1, 0);
        /* search buckets for alternate index */
        for (i = 0; i < mdc_tbl->bucket; i++) {
            if (tbl[offset].occupied && addr == tbl[offset].addr) {
                *label = tbl[offset].label;
                return 0;
            }

            offset++;
        }
    }

    return ret;
}

static inline int aws_mdc_mod_entry(struct aws_mdc_table *mdc_tbl, uint64_t addr,
                                    mdc_label_t new_label) {
    size_t i;
    int ret = -ENOENT;
    uint32_t hash, idx1, offset;
    struct aws_mdc_entry *tbl = mdc_tbl->table;

    hash = aws_mdc_hash(addr);
    idx1 = aws_mdc_hash_to_index(hash, mdc_tbl->size);

    offset = aws_mdc_ib_to_offset(mdc_tbl, idx1, 0);

    if (mdc_tbl->bucket == 4) {
        int tmp1 = aws_find_index(addr, &tbl[offset].entry, mdc_tbl->count);
        if (tmp1) {
            tbl[offset + tmp1 - 1].label = new_label;
            return 0;
        }

        idx1 = aws_mdc_alt_index(hash, mdc_tbl->size_power, idx1);
        offset = aws_mdc_ib_to_offset(mdc_tbl, idx1, 0);

        int tmp2 = aws_find_index(addr, &tbl[offset].entry, mdc_tbl->count);

        if (tmp2) {
            tbl[offset + tmp2 - 1].label = new_label;
            return 0;
        }

    } else {
        /* search buckets for first index */
        for (i = 0; i < mdc_tbl->bucket; i++) {
            if (tbl[offset].occupied && addr == tbl[offset].addr) {
                tbl[offset].label = new_label;
                return 0;
            }

            offset++;
        }

        idx1 = aws_mdc_alt_index(hash, mdc_tbl->size_power, idx1);
        offset = aws_mdc_ib_to_offset(mdc_tbl, idx1, 0);
        /* search buckets for alternate index */
        for (i = 0; i < mdc_tbl->bucket; i++) {
            if (tbl[offset].occupied && addr == tbl[offset].addr) {
                tbl[offset].label = new_label;
                return 0;
            }

            offset++;
        }
    }

    return ret;
}


//static int mdc_find_offset(struct mdc_table *mdc_tbl, uint64_t addr,
//                    uint32_t *offset_out) {
//    size_t i;
//    uint32_t hash, idx1, offset;
//    struct mdc_entry *tbl = mdc_tbl->table;
//
//    hash = mdc_hash(addr);
//    idx1 = mdc_hash_to_index(hash, mdc_tbl->size);
//
//    offset = mdc_ib_to_offset(mdc_tbl, idx1, 0);
//    /* search buckets for first index */
//    for (i = 0; i < mdc_tbl->bucket; i++) {
//        if (tbl[offset].occupied && addr == tbl[offset].addr) {
//            *offset_out = offset;
//            return 0;
//        }
//
//        offset++;
//    }
//
//    idx1 = mdc_alt_index(hash, mdc_tbl->size_power, idx1);
//    offset = mdc_ib_to_offset(mdc_tbl, idx1, 0);
//    /* search buckets for alternate index */
//    for (i = 0; i < mdc_tbl->bucket; i++) {
//        if (tbl[offset].occupied && addr == tbl[offset].addr) {
//            *offset_out = offset;
//            return 0;
//        }
//
//        offset++;
//    }
//
//    return -ENOENT;
//}

static int aws_mdc_find_slot(struct aws_mdc_table *mdc_tbl, mac_addr_t addr, uint32_t *idx,
                             uint32_t *bucket) {
    size_t i, j;
    uint32_t hash;
    uint32_t idx1, idx_v1, idx_v2;
    uint32_t offset1, offset2;
    struct aws_mdc_entry *tbl = mdc_tbl->table;

    hash = aws_mdc_hash(addr);
    idx1 = aws_mdc_hash_to_index(hash, mdc_tbl->size);

    /* if there is available slot */
    for (i = 0; i < mdc_tbl->bucket; i++) {
        offset1 = aws_mdc_ib_to_offset(mdc_tbl, idx1, i);
        if (!tbl[offset1].occupied) {
            *idx = idx1;
            *bucket = i;
            return 0;
        }
    }

    offset1 = aws_mdc_ib_to_offset(mdc_tbl, idx1, 0);

    /* try moving */
    for (i = 0; i < mdc_tbl->bucket; i++) {
        offset1 = aws_mdc_ib_to_offset(mdc_tbl, idx1, i);
        hash = aws_mdc_hash(tbl[offset1].addr);
        idx_v1 = aws_mdc_hash_to_index(hash, mdc_tbl->size);
        idx_v2 = aws_mdc_alt_index(hash, mdc_tbl->size_power, idx_v1);

        /* if the alternate bucket is same as original skip it */
        if (idx_v1 == idx_v2 || idx1 == idx_v2)
            break;

        for (j = 0; j < mdc_tbl->bucket; j++) {
            offset2 = aws_mdc_ib_to_offset(mdc_tbl, idx_v2, j);
            if (!tbl[offset2].occupied) {
                /* move offset1 to offset2 */
                tbl[offset2] = tbl[offset1];
                /* clear offset1 */
                tbl[offset1].occupied = 0;

                *idx = idx1;
                *bucket = 0;
                return 0;
            }
        }
    }

    /* TODO:if alternate index is also full then start move */
    return -ENOMEM;
}

static int aws_mdc_add_entry(struct aws_mdc_table *mdc_tbl, mac_addr_t addr,
                             mdc_label_t label) {
    uint32_t offset;
    uint32_t index;
    uint32_t bucket;
    mdc_label_t label_tmp;

    /* if addr already exist then fail */
    if (aws_mdc_find(mdc_tbl, addr, &label_tmp) == 0) {
        return -EEXIST;
    }

    /* find slots to put entry */
    if (aws_mdc_find_slot(mdc_tbl, addr, &index, &bucket) != 0) {
        return -ENOMEM;
    }

    /* insert entry into empty slot */
    offset = aws_mdc_ib_to_offset(mdc_tbl, index, bucket);

    mdc_tbl->table[offset].addr = addr;
    mdc_tbl->table[offset].label = label;
    mdc_tbl->table[offset].occupied = 1;
    mdc_tbl->count++;
    return 0;
}

//static int mdc_del_entry(struct mdc_table *mdc_tbl, uint64_t addr) {
//    uint32_t offset = 0xFFFFFFFF;
//
//    if (mdc_find_offset(mdc_tbl, addr, &offset)) {
//        return -ENOENT;
//    }
//
//    mdc_tbl->table[offset].addr = 0;
//    mdc_tbl->table[offset].label = 0;
//    mdc_tbl->table[offset].occupied = 0;
//    mdc_tbl->count--;
//    return 0;
//}

//static int mdc_flush(struct mdc_table *mdc_tbl) {
//    if (nullptr == mdc_tbl || nullptr == mdc_tbl->table) {
//        return -EINVAL;
//    }
//
//    memset(mdc_tbl->table, 0,
//           sizeof(struct mdc_entry) * mdc_tbl->size * mdc_tbl->bucket);
//
//    return 0;
//}

static uint64_t aws_mdc_addr_to_u64(char *addr) {
    uint64_t a = *(reinterpret_cast<uint8_t *>(addr));
    uint64_t b = *(reinterpret_cast<uint16_t *>(addr + 1));

    return a | (b << 8);
}


static int aws_parse_mac_addr(const char *str, char *addr) {
    if (str != nullptr && addr != nullptr) {
        int r = sscanf(str, "%2hhx:%2hhx", addr, addr + 1);
        if (r != 2) {
            return -EINVAL;
        }
    }

    return 0;
}


/******************************************************************************/


const Commands AwsMdcReceiver::cmds = {
        {"add",   "AwsMdcReceiverCommandAddArg",
                              MODULE_CMD_FUNC(&AwsMdcReceiver::CommandAdd),   Command::THREAD_UNSAFE},
        {"clear", "EmptyArg", MODULE_CMD_FUNC(&AwsMdcReceiver::CommandClear), Command::THREAD_UNSAFE},
};

CommandResponse
AwsMdcReceiver::Init(const sample::aws_mdc_receiver::pb::AwsMdcReceiverArg &arg) {
    int ret = 0;
    int size = AWS_MDC_DEFAULT_TABLE_SIZE;
    int bucket = AWS_MDC_MAX_BUCKET_SIZE;

    // Parses Agent ID
//    if (arg.agent_id() < 0) {
//        return CommandFailure(-1, "Agent ID has to be zero or a +ve integer");
//    }
    agent_id_ = arg.agent_id();

    if (arg.agent_label() <= 0) {
        return CommandFailure(-1, "Agent Label has to be a +ve integer");
    }
    agent_label_ = arg.agent_label() << 24;

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

    // Initialize the table
    ret = aws_mdc_init(&mdc_table_, size, bucket);
    if (ret != 0) {
        return CommandFailure(-ret,
                              "MAC-Label table initialization failed with argument "
                              "size: '%d' bucket: '%d'",
                              size, bucket);
    }
    return CommandSuccess();

}

void AwsMdcReceiver::DeInit() {
    aws_mdc_deinit(&mdc_table_);
}

CommandResponse AwsMdcReceiver::CommandAdd(
        const sample::aws_mdc_receiver::pb::AwsMdcReceiverCommandAddArg &arg) {

    for (int i = 0; i < arg.entries_size(); i++) {
        const auto &entry = arg.entries(i);

        if (!entry.addr().length()) {
            return CommandFailure(EINVAL,
                                  "add list item map must contain addr as a string");
        }

        const char *str_addr = entry.addr().c_str();
        int label = entry.label();
        char addr[2];

        if (aws_parse_mac_addr(str_addr, addr) != 0) {
            return CommandFailure(EINVAL, "%s is not a proper mac address", str_addr);
        }

        int r = aws_mdc_add_entry(&mdc_table_, aws_mdc_addr_to_u64(addr), label);
//        std::cout << r << str_addr << aws_mdc_addr_to_u64(addr) << label;
        if (r == -EEXIST) {
            return CommandFailure(EEXIST, "MAC address '%s' already exist", str_addr);
        } else if (r == -ENOMEM) {
            return CommandFailure(ENOMEM, "Not enough space");
        } else if (r != 0) {
            return CommandFailure(-r);
        }
    }

    return CommandSuccess();
}

CommandResponse AwsMdcReceiver::CommandClear(const bess::pb::EmptyArg &) {
    return CommandSuccess();
}

void AwsMdcReceiver::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
    int cnt = batch->cnt();
    for (int i = 0; i < cnt; i++) {
//        gate_idx_t i_gate = ctx->current_igate;
        // i_gate = 0: external
        // i_gate = 1: internal
        // i_gate = 2: generated ping pkts

        bess::Packet *pkt = batch->pkts()[i];
        Ethernet *eth = pkt->heavimd_data<Ethernet *>();

        // If it's an ARP pkt
        if (eth->ether_type != be16_t(Ethernet::Type::kIpv4)) {
            if (eth->ether_type == be16_t(Ethernet::Type::kArp)) {
                Arp *arp = reinterpret_cast<Arp *>(eth + 1);
                if (arp->opcode == be16_t(Arp::Opcode::kRequest)) {
                    arp->opcode = be16_t(Arp::Opcode::kReply);

                    eth->dst_addr = eth->src_addr;
                    eth->src_addr = agent_mac_;

                    arp->target_hw_addr = arp->sender_hw_addr;
                    arp->sender_hw_addr = agent_mac_;

                    arp->target_ip_addr = arp->sender_ip_addr;
                    arp->sender_ip_addr = agent_ip_;
                    EmitPacket(ctx, pkt, 0);
                    continue;
                }
            } else {
                DropPacket(ctx, pkt);
                continue;
            }
        }

        Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
        // If it is not a UDP pkt
        if (ip->protocol != Ipv4::Proto::kUdp) {
            DropPacket(ctx, pkt);
            continue;
        }

        int ip_bytes = ip->header_length << 2;
        // Access UDP payload (i.e., mDC data)
        be64_t *p = pkt->head_data<be64_t *>(sizeof(Ethernet) + ip_bytes + sizeof(Udp));

        uint8_t mdc_type = p->raw_value() & 0x00000000000000ff;
        // A data packet
        if (mdc_type == 0xff) {
            // did it come from internal or external port

            // Data pkts
            mac_addr_t address = (p->raw_value() & 0x0000000000ffff00) >> 8;
            mdc_mode_t mode = (p->raw_value() & 0x0000ff0000000000) >> 40;
            if (mode == 0x00) {
                // If mode is 0x00, the data pkt needs to be labeled
                mdc_label_t out_label = 0x00;
                int ret = aws_mdc_find(&mdc_table_, address, &out_label);
                if (ret != 0) {
                    out_label = 0x00;
                }

                bool agent_is_only_recv = (~agent_label_ & ~out_label) == ~agent_label_;

                // If the Agent host has a receiver, copy the pkt and send it to Gate 1.
                if ((agent_label_ & out_label) == agent_label_) {
                    if (!agent_is_only_recv) {
                        bess::Packet *new_pkt = bess::Packet::copy(pkt);
                        if (new_pkt) {
                            EmitPacket(ctx, new_pkt, 1);
                        }
                    } else {
                        EmitPacket(ctx, pkt, 1);
                    }
                }

                if (agent_is_only_recv) {
                    continue;
                }

                // Label the pkt, make sure to remove the agent ID label from the final label
                *p = *p | (be64_t(out_label & ~agent_label_) >> 16);
                *p = *p | be64_t(0x0000000000ff0000);

                eth->dst_addr = switch_mac_;
                ip->dst = switch_ip_;

                eth->src_addr = agent_mac_;
                ip->src = agent_ip_;

                EmitPacket(ctx, pkt, 0);
            } else {
                EmitPacket(ctx, pkt, 1);
            }
        } else {
            // A control pkt
            if (mdc_type == 0xf1) {
                // A pong pkt
                emit_ping_pkt_ = true;
            } else if (mdc_type == 0xf0) {
                // A ping pkt coming from generator
                // We need to make sure the mDC Agent still sends pkts even if ToR had failed, in case the ToR comes back!
                // "100" represents the rate at which the Agent resends the ping pkts if ToR fails
                // This value needs to be adaptive or configured
                if (emit_ping_pkt_ || gen_ping_pkts_count_ == 100) {
                    EmitPacket(ctx, pkt, 0);
                    gen_ping_pkts_count_ = 0;
                    emit_ping_pkt_ = false;
                }
                gen_ping_pkts_count_ += 1;
            } else if (mdc_type == 0xf2) {
                // sync-state pkt
                int ret = -1;
                mdc_label_t tmp_label;
                mac_addr_t address = (p->raw_value() & 0x0000000000ffff00) >> 8;
                mdc_label_t new_label = (p->raw_value() & 0x00ffffffff000000) >> 24;

                // if the session is already stored, modify it. Else, add it to the table.
                if (aws_mdc_find(&mdc_table_, address, &tmp_label) == 0) {
                    ret = aws_mdc_mod_entry(&mdc_table_, address, new_label);
                } else {
                    ret = aws_mdc_add_entry(&mdc_table_, address, new_label);
                }
                if (ret == 0) {
                    // send the pkt back to the switch
                    // set the label to 0x00000000
                    *p = *p & be64_t(0xffffff00000000ff);
                    // set the mdc_type to 0xf3
                    *p = *p | be64_t(0xf300000000000000);
                    // set the current Agent ID
                    *p = *p | (be64_t(agent_id_) << 32);
                    EmitPacket(ctx, pkt, 0);
                } else {
                    DropPacket(ctx, pkt);
                }
            }
        }
    }
}

ADD_MODULE(AwsMdcReceiver, "aws_mdc_receiver", "processing MDC pkts in AWS hosts")
