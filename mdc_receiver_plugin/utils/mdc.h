// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// Copyright (c) 2017, Cloudigo.
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

#ifndef BESS_UTILS_MDC_H_
#define BESS_UTILS_MDC_H_

#include <cstdint>
#include <string>
#include <type_traits>

#include "endian.h"

namespace bess {
    namespace utils {

        struct [[gnu::packed]] Mdc {
            static const uint16_t kDataLabeledType = 0xdd00;
            static const uint16_t kDataUnlabeledType = 0xdd01;
            static const uint16_t kControlSetAgentType = 0xcc00;
            static const uint16_t kControlPingType = 0xcc01;
            static const uint16_t kControlPongType = 0xcc02;
            static const uint16_t kControlSyncStateType = 0xcc03;
            static const uint16_t kControlSyncStateDoneType = 0xcc04;

        };

    }  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_MDC_H_
