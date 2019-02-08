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
        {"add",   "MdcReceiverArg",
                              MODULE_CMD_FUNC(&MdcReceiver::CommandAdd), Command::THREAD_UNSAFE},
        {"clear", "EmptyArg", MODULE_CMD_FUNC(&MdcReceiver::CommandClear),
                                                                         Command::THREAD_UNSAFE},
};

CommandResponse
MdcReceiver::Init(const sample::mdc_receiver::pb::MdcReceiverArg &) {
    int ret = 0;
    int size = MDC_DEFAULT_TABLE_SIZE;
    int bucket = MDC_MAX_BUCKET_SIZE;

    ret = mdc_init(&mdc_table_, size, bucket);

    if (ret != 0) {
        return CommandFailure(-ret,
                              "initialization failed with argument "
                              "size: '%d' bucket: '%d'",
                              size, bucket);
    }
    return CommandSuccess();

}

void MdcReceiver::DeInit() {
    mdc_deinit(&mdc_table_);
}

CommandResponse MdcReceiver::CommandAdd(
        const sample::mdc_receiver::pb::MdcReceiverArg &arg) {

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

        int r = mdc_add_entry(&mdc_table_, mdc_addr_to_u64(addr), label);
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

CommandResponse MdcReceiver::CommandClear(const bess::pb::EmptyArg &) {
    return CommandSuccess();
}

void MdcReceiver::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {

    int cnt = batch->cnt();

    for (int i = 0; i < cnt; i++) {
        bess::Packet *pkt = batch->pkts()[i];
        Ethernet *eth = pkt->head_data<Ethernet *>();

        if (eth->ether_type == be16_t(Mdc::kDataType)) {
            mdc_label_t out_label = 0x0a0b0c;
            int ret = mdc_find(&mdc_table_,
                               *(pkt->head_data<uint64_t *>()) & 0x0000ffffffffffff,
                               &out_label);
            if (ret != 0) {
                out_label = 0x0a0b0c;
            }

            be32_t *p = pkt->head_data<be32_t *>(sizeof(Ethernet));
            *p = (be32_t(1) << 24) | be32_t(out_label);
        } else if (eth->ether_type == be16_t(Mdc::kControlType)) {

        } else {
            // non-MDC packets are dropped
            DropPacket(ctx, pkt);
            continue;
        }
    }

    RunNextModule(ctx, batch);
}

ADD_MODULE(MdcReceiver, "mdc_receiver",
           "processing MDC pkts")
