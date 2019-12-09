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

#include "mdc_receiver.h"


static int is_power_of_2(uint64_t n) {
    return (n != 0 && ((n & (n - 1)) == 0));
}

static int mdc_init(struct mdc_table *mcd_tbl, int size, int bucket) {
    if (size <= 0 || size > MDC_MAX_TABLE_SIZE || !is_power_of_2(size)) {
        return -EINVAL;
    }

    if (bucket <= 0 || bucket > MDC_MAX_BUCKET_SIZE || !is_power_of_2(bucket)) {
        return -EINVAL;
    }

    if (mcd_tbl == nullptr) {
        return -EINVAL;
    }

    mcd_tbl->table = new(std::nothrow) mdc_entry[size * bucket]{};

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

static int mdc_deinit(struct mdc_table *mdc_tbl) {
    if (mdc_tbl == nullptr || mdc_tbl->table == nullptr || mdc_tbl->size == 0 ||
        mdc_tbl->bucket == 0) {
        return -EINVAL;
    }

    delete[] mdc_tbl->table;
    *mdc_tbl = {};
    return 0;
}

static uint32_t mdc_ib_to_offset(struct mdc_table *mdc_tbl, int index, int bucket) {
    return index * mdc_tbl->bucket + bucket;
}

static uint32_t mdc_hash(mac_addr_t addr) {
    return rte_hash_crc_8byte(addr, 0);
}

static uint32_t mdc_hash_to_index(uint32_t hash, uint32_t size) {
    return hash & (size - 1);
}

static uint32_t mdc_alt_index(uint32_t hash, uint32_t size_power,
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
static inline int find_index_avx(uint64_t addr, uint64_t *table) {
  DCHECK(reinterpret_cast<uintptr_t>(table) % 32 == 0);
  __m256d _addr = (__m256d)_mm256_set1_epi64x(addr | (1ull << 63));
  __m256d _table = _mm256_load_pd((double *)table);
  _table = _mm256_and_pd(_table, _mask._mask);
  __m256d cmp = _mm256_cmp_pd(_addr, _table, _CMP_EQ_OQ);

  return __builtin_ffs(_mm256_movemask_pd(cmp));
}
#else

static inline int find_index_basic(uint64_t addr, uint64_t *table) {
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
static inline int find_index(uint64_t addr, uint64_t *table, const uint64_t) {
#if __AVX__
    return find_index_avx(addr, table);
#else
    return find_index_basic(addr, table);
#endif
}

static inline int mdc_find(struct mdc_table *mdc_tbl, uint64_t addr,
                           mdc_label_t *label) {
    size_t i;
    int ret = -ENOENT;
    uint32_t hash, idx1, offset;
    struct mdc_entry *tbl = mdc_tbl->table;

    hash = mdc_hash(addr);
    idx1 = mdc_hash_to_index(hash, mdc_tbl->size);

    offset = mdc_ib_to_offset(mdc_tbl, idx1, 0);

    if (mdc_tbl->bucket == 4) {
        int tmp1 = find_index(addr, &tbl[offset].entry, mdc_tbl->count);
        if (tmp1) {
            *label = tbl[offset + tmp1 - 1].label;
            return 0;
        }

        idx1 = mdc_alt_index(hash, mdc_tbl->size_power, idx1);
        offset = mdc_ib_to_offset(mdc_tbl, idx1, 0);

        int tmp2 = find_index(addr, &tbl[offset].entry, mdc_tbl->count);

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

        idx1 = mdc_alt_index(hash, mdc_tbl->size_power, idx1);
        offset = mdc_ib_to_offset(mdc_tbl, idx1, 0);
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

static inline int mdc_mod_entry(struct mdc_table *mdc_tbl, uint64_t addr,
                                    mdc_label_t new_label) {
    size_t i;
    int ret = -ENOENT;
    uint32_t hash, idx1, offset;
    struct mdc_entry *tbl = mdc_tbl->table;

    hash = mdc_hash(addr);
    idx1 = mdc_hash_to_index(hash, mdc_tbl->size);

    offset = mdc_ib_to_offset(mdc_tbl, idx1, 0);

    if (mdc_tbl->bucket == 4) {
        int tmp1 = find_index(addr, &tbl[offset].entry, mdc_tbl->count);
        if (tmp1) {
            tbl[offset + tmp1 - 1].label = new_label;
            return 0;
        }

        idx1 = mdc_alt_index(hash, mdc_tbl->size_power, idx1);
        offset = mdc_ib_to_offset(mdc_tbl, idx1, 0);

        int tmp2 = find_index(addr, &tbl[offset].entry, mdc_tbl->count);

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

        idx1 = mdc_alt_index(hash, mdc_tbl->size_power, idx1);
        offset = mdc_ib_to_offset(mdc_tbl, idx1, 0);
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

static int mdc_find_slot(struct mdc_table *mdc_tbl, mac_addr_t addr, uint32_t *idx,
                         uint32_t *bucket) {
    size_t i, j;
    uint32_t hash;
    uint32_t idx1, idx_v1, idx_v2;
    uint32_t offset1, offset2;
    struct mdc_entry *tbl = mdc_tbl->table;

    hash = mdc_hash(addr);
    idx1 = mdc_hash_to_index(hash, mdc_tbl->size);

    /* if there is available slot */
    for (i = 0; i < mdc_tbl->bucket; i++) {
        offset1 = mdc_ib_to_offset(mdc_tbl, idx1, i);
        if (!tbl[offset1].occupied) {
            *idx = idx1;
            *bucket = i;
            return 0;
        }
    }

    offset1 = mdc_ib_to_offset(mdc_tbl, idx1, 0);

    /* try moving */
    for (i = 0; i < mdc_tbl->bucket; i++) {
        offset1 = mdc_ib_to_offset(mdc_tbl, idx1, i);
        hash = mdc_hash(tbl[offset1].addr);
        idx_v1 = mdc_hash_to_index(hash, mdc_tbl->size);
        idx_v2 = mdc_alt_index(hash, mdc_tbl->size_power, idx_v1);

        /* if the alternate bucket is same as original skip it */
        if (idx_v1 == idx_v2 || idx1 == idx_v2)
            break;

        for (j = 0; j < mdc_tbl->bucket; j++) {
            offset2 = mdc_ib_to_offset(mdc_tbl, idx_v2, j);
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

static int mdc_add_entry(struct mdc_table *mdc_tbl, mac_addr_t addr,
                         mdc_label_t label) {
    uint32_t offset;
    uint32_t index;
    uint32_t bucket;
    mdc_label_t label_tmp;

    /* if addr already exist then fail */
    if (mdc_find(mdc_tbl, addr, &label_tmp) == 0) {
        return -EEXIST;
    }

    /* find slots to put entry */
    if (mdc_find_slot(mdc_tbl, addr, &index, &bucket) != 0) {
        return -ENOMEM;
    }

    /* insert entry into empty slot */
    offset = mdc_ib_to_offset(mdc_tbl, index, bucket);

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

static uint64_t mdc_addr_to_u64(char *addr) {
    uint64_t a = *(reinterpret_cast<uint32_t *>(addr));
    uint64_t b = *(reinterpret_cast<uint16_t *>(addr + 4));

    return a | (b << 32);
}

static int parse_mac_addr(const char *str, char *addr) {
    if (str != nullptr && addr != nullptr) {
        int r = sscanf(str, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx", addr, addr + 1,
                       addr + 2, addr + 3, addr + 4, addr + 5);

        if (r != 6) {
            return -EINVAL;
        }
    }

    return 0;
}

/******************************************************************************/


const Commands MdcReceiver::cmds = {
        {"add",   "MdcReceiverCommandAddArg",
                              MODULE_CMD_FUNC(&MdcReceiver::CommandAdd), Command::THREAD_UNSAFE},
        {"clear", "EmptyArg", MODULE_CMD_FUNC(&MdcReceiver::CommandClear),
                                                                         Command::THREAD_UNSAFE},
};

CommandResponse
MdcReceiver::Init(const sample::mdc_receiver::pb::MdcReceiverArg &arg) {
    int ret = 0;
    int size = MDC_DEFAULT_TABLE_SIZE;
    int bucket = MDC_MAX_BUCKET_SIZE;

    ip_encap_ = arg.ip_encap();

    // Parses Agent ID
    if(arg.agent_id() <= 0) {
        return CommandFailure(-1, "Agent ID has to be a positive integer");
    }
    /* TODO @parham: Make sure we still use both agent_id and agent_label */
    if (arg.agent_label() <= 0) {
        return CommandFailure(-1, "Agent Label has to be a +ve integer");
    }

    const std::string &switch_mac_str = arg.switch_mac();
    const std::string &agent_mac_str = arg.agent_mac();

    switch_mac_ = Ethernet::Address(switch_mac_str);
    agent_mac_ = Ethernet::Address(agent_mac_str);

    agent_id_ = arg.agent_id();
    agent_label_ = arg.agent_label(); // << 24;

    // Initialize the table
    ret = mdc_init(&mdc_table_, size, bucket);
    if (ret != 0) {
        return CommandFailure(-ret,
                              "MAC-Label table initialization failed with argument "
                              "size: '%d' bucket: '%d'",
                              size, bucket);
    }
    return CommandSuccess();
}

void MdcReceiver::DeInit() {
    mdc_deinit(&mdc_table_);
}

CommandResponse MdcReceiver::CommandAdd(
        const sample::mdc_receiver::pb::MdcReceiverCommandAddArg &arg) {

    for (int i = 0; i < arg.entries_size(); i++) {
        const auto &entry = arg.entries(i);

        if (!entry.addr().length()) {
            return CommandFailure(EINVAL,
                                  "add list item map must contain addr as a string");
        }

        const char *str_addr = entry.addr().c_str();
        int label = entry.label();
        char addr[6];

        if (parse_mac_addr(str_addr, addr) != 0) {
            return CommandFailure(EINVAL, "%s is not a proper mac address", str_addr);
        }
	// std::cout << mdc_addr_to_u64(addr) << label << std::endl;
        int r = mdc_add_entry(&mdc_table_, mdc_addr_to_u64(addr), label);
        if (r == -EEXIST) {
            return CommandFailure(EEXIST, "MAC address '%s' already exist", str_addr);
        } else if (r == -ENOMEM) {
            return CommandFailure(ENOMEM, "Not enough space");
        } else if (r != 0) {
            return CommandFailure(-r);
        }
///*
	mdc_label_t out_label1 = 0x00;
int ret1 = -1;
ret1 = mdc_find(&mdc_table_, 55465664714246, &out_label1);
if (ret1 != 0) {                                                                                                             
std::cout << ":(" << std::endl;
out_label1 = 0x00; 
}
// std::cout << std::hex << out_label1 << std::dec << std::endl;
//*/
    }

    return CommandSuccess();
}

CommandResponse MdcReceiver::CommandClear(const bess::pb::EmptyArg &) {
    return CommandSuccess();
}

void MdcReceiver::LabelAndSendPacket(Context *ctx, bess::Packet *pkt){
    // This is the dst_mac address from Ethernet header.
    uint64_t start_tsc = rdtsc();
    mac_addr_t address = *(pkt->head_data<uint64_t *>()) & 0x0000ffffffffffff;
    mdc_label_t out_label = 0x50;
    int ret = -1;
    bool agent_is_only_recv = 0;
    
    ret = mdc_find(&mdc_table_, address, &out_label);
    if (ret != 0) {
	std::cout << ":(" << std::endl;
        out_label = 0x00;
    }
    mdc_label_t tor_label = out_label & 0x000000ff; 
    agent_is_only_recv = ((~agent_label_ & ~tor_label) == ~agent_label_);              
    if((agent_id_ & tor_label) == agent_id_) { // Agent is in destination list
        if (!agent_is_only_recv) {
            bess::Packet *newpkt = bess::Packet::copy(pkt);
            if (newpkt) {
                EmitPacket(ctx, newpkt, MDC_OUTPUT_INTERNAL);
            }
        } else {
                EmitPacket(ctx, pkt, MDC_OUTPUT_INTERNAL);
        }
    }
    // std::cout << "LABEL" << out_label << std::endl;
    // std::cout << (~agent_label_ & ~tor_label) << std::endl;
    // std::cout << ~agent_label_  << std::endl;
    if (agent_is_only_recv) {
	//std::cout << "HI" << std::endl;
        return;
    } 
    //else {
    //std::cout << "BYE" << std::endl;
    //}

    be64_t *p1 = pkt->head_data<be64_t *>(sizeof(Ethernet));
    if(!ip_encap_) {
        // label the pkt and remove agent_label_ from out_label
        *p1 = *p1 | (be64_t(out_label & ~agent_label_));
        // Set packet type as 0x02 (MDC_TYPE_LABELED)
        *p1 = *p1 & be64_t(0x00ffffffffffffff); // clear type bits
        *p1 = *p1 | be64_t(0x0200000000000000); // Set 0x02
    } else {
        // Set packet type as 0x02 (MDC_TYPE_LABELED)
        *p1 = *p1 & be64_t(0xffffffff00ffffff); // clear type bits
        *p1 = *p1 | be64_t(0x0000000002000000); // Set 0x02

        be64_t *p2 = p1 + 1;
        *p2 = *p2 | ((be64_t(out_label & ~agent_label_)) << 32);
    }
    uint64_t end_tsc = rdtsc() - start_tsc;
    EmitPacket(ctx, pkt, MDC_OUTPUT_TOR);

    if (unlikely(!p_latency_first_pkt_rec_ns_)) {
        p_latency_first_pkt_rec_ns_ = start_tsc;
    }

    if (p_latency_enabled_) {
        // Pkt received
        p_latency_rtt_hist_.Insert(end_tsc);
        // a hack to collect results
        if (p_latency_first_pkt_rec_ns_ && (start_tsc - p_latency_first_pkt_rec_ns_) >= 60'000'000'000) {
            p_latency_enabled_ = false;
            std::vector<double> latency_percentiles{50, 95, 99, 99.9, 99.99};
            const auto &rtt = p_latency_rtt_hist_.Summarize(latency_percentiles);
            std::cout << rtt.avg;
            std::cout << rtt.min;
            std::cout << rtt.max;
            for (int percentile_value : rtt.percentile_values)
                std::cout << percentile_value;
        }
    }
}

void MdcReceiver::DoProcessAppBatch(Context *ctx, bess::PacketBatch *batch) {
    /* Process internal packets */
    int cnt = batch->cnt();
    
    for (int i = 0; i < cnt; i++) {
        bess::Packet *pkt = batch->pkts()[i];
        LabelAndSendPacket(ctx, pkt);
    }
}

void MdcReceiver::DoProcessExtBatch(Context *ctx, bess::PacketBatch *batch) {
    /* Process external packets */
    // TODO: All Sync-related messages aren't tested yet.
    int cnt = batch->cnt();

    for (int i = 0; i < cnt; i++) {
        bess::Packet *pkt = batch->pkts()[i];
        be64_t *p = pkt->head_data<be64_t *>(sizeof(Ethernet));
        uint8_t mdc_type = p->raw_value() & MDC_PKT_TYPE_MASK;
        if(ip_encap_) {
            mdc_type = (p->raw_value() & MDC_PKT_IP_TYPE_MASK) >> 32;
        }

	// std::cout << ((p->raw_value() & MDC_PKT_IP_TYPE_MASK) >> 32) << std::endl;
	// std::cout << std::hex << *p << std::dec << std::endl;

        int ret = -1;
        mac_addr_t address;
        mdc_label_t tmp_label, new_label;

        switch(mdc_type) {
            case MDC_TYPE_UNLABELED: // It is a data pkt and needs to be labeled
                LabelAndSendPacket(ctx, pkt);
                break;
            case MDC_TYPE_LABELED: // It's a data pkt and labled so send to FileWriter module
                EmitPacket(ctx, pkt, MDC_OUTPUT_INTERNAL);
                break;
            case MDC_TYPE_PONG:
                emit_ping_pkt_ = true;
		        DropPacket(ctx, pkt);
                break;
            case MDC_TYPE_PING:
                // A ping pkt coming from generator
                // We need to make sure the mDC Agent still sends pkts even if ToR had failed, in case the ToR comes back!
                // "100" represents the rate at which the Agent resends the ping pkts if ToR fails
                // This value needs to be adaptive or configured
                if (emit_ping_pkt_ || gen_ping_pkts_count_ == 100) {
                    EmitPacket(ctx, pkt, MDC_OUTPUT_TOR);
                    gen_ping_pkts_count_ = 0;
                    emit_ping_pkt_ = false;
                }
                gen_ping_pkts_count_ += 1;
                break;
            case MDC_TYPE_SYNC_STATE:
                new_label = (p->raw_value() & MDC_PKT_LABEL_MASK) >> 32;
                address = (p->raw_value() & MDC_PKT_ADDRESS_MASK) >> 16;
                // if the session is already stored, modify it. Else, add it to the table.
                if (mdc_find(&mdc_table_, address, &tmp_label) == 0) {
                    ret = mdc_mod_entry(&mdc_table_, address, new_label);
                } else {
                    ret = mdc_add_entry(&mdc_table_, address, new_label);
                }
                if (ret == 0) {
                    // send the pkt back to the switch
                    // set the label to 0x00000000
                    *p = *p & be64_t(0xffffffff00000000);
                    /* 
                     TODO: This line works for changing 0xf2 to 0xf3 but if convention changed,
                     bitwise or doesn't always set the desired value 
                     */
                    // set the mdc_type to 0xf3
                    *p = *p | be64_t(0xf300000000000000);
                    // set the current Agent ID
                    *p = *p | (be64_t(agent_id_) << 48); // 6 byte shift since "type" is before agent id
                    EmitPacket(ctx, pkt, MDC_OUTPUT_TOR);
                } else {
                    DropPacket(ctx, pkt);
                }
                break;
            default:
                DropPacket(ctx, pkt);
                break;
        }
    }
}

void MdcReceiver::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
    gate_idx_t incoming_gate = ctx->current_igate;

    if (incoming_gate == MDC_INPUT_APP) {
        DoProcessAppBatch(ctx, batch);
    } else {
        DoProcessExtBatch(ctx, batch);
    }

//     int cnt = batch->cnt();

//     for (int i = 0; i < cnt; i++) {
//         bess::Packet *pkt = batch->pkts()[i];
//         Ethernet *eth = pkt->head_data<Ethernet *>();

//         // Data pkts
//         if (eth->ether_type == be16_t(Mdc::kDataType)) {
//             be32_t *p = pkt->head_data<be32_t *>(sizeof(Ethernet));
//             uint8_t mode = p->raw_value() & 0x000000ff;
// //            std::cout << std::hex << static_cast<int>(mode) << std::endl;

//             if (mode == 0x00) {
//                 // If mode is 0x00, the data pkt needs to be labeled
//                 mdc_label_t out_label = 0x0a0b0c;
//                 int ret = mdc_find(&mdc_table_,
//                                    *(pkt->head_data<uint64_t *>()) & 0x0000ffffffffffff,
//                                    &out_label);
//                 if (ret != 0) {
//                     out_label = 0x0a0b0c;
//                 }

//                 // If the Agent host has a receiver, copy the pkt and send it to Gate 1.
//                 if((agent_id_ & out_label) == agent_id_) {
//                     bess::Packet *newpkt = bess::Packet::copy(pkt);
//                     if (newpkt) {
//                         EmitPacket(ctx, newpkt, 1);
//                     }
//                 }
//                 // TODO: if agent is the only receiver, don't emit pkt again

//                 // Label the pkt, make sure to remove the agent ID label from the final label
//                 *p = (be32_t(0xff) << 24) | be32_t(out_label & ~agent_id_);
//                 EmitPacket(ctx, pkt, 0);
//             }
//             //TODO receive the pkt here
//         } else if (eth->ether_type == be16_t(Mdc::kControlStateType)) {
//             // TODO: update state
//             EmitPacket(ctx, pkt, 0);
//         } else if (eth->ether_type == be16_t(Mdc::kControlHealthType)) {
//             EmitPacket(ctx, pkt, 2);
//         } else {
//             // non-MDC packets are dropped
//             DropPacket(ctx, pkt);
//             continue;
//         }
//     }
}

ADD_MODULE(MdcReceiver, "mdc_receiver",
           "processing MDC pkts")
