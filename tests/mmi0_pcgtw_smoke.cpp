// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/runtime/ee_state.hpp"

#include <cstdint>

namespace reps2::generated {
[[nodiscard]] reps2::runtime::Status execute(reps2::runtime::EeState& state) noexcept;
}

namespace {

using reps2::runtime::EeState;
using reps2::runtime::Gpr128;
using reps2::runtime::Status;

constexpr Gpr128 kLhs{0x7FFF'FFFF'8000'0000ULL, 0x0000'0005'FFFF'FFFFULL};
constexpr Gpr128 kRhs{0x8000'0000'7FFF'FFFFULL, 0xFFFF'FFFE'0000'0001ULL};
constexpr Gpr128 kExpected{0xFFFF'FFFF'0000'0000ULL, 0xFFFF'FFFF'0000'0000ULL};
constexpr Gpr128 kZero{0U, 0U};
constexpr Gpr128 kSentinel{0x1234'5678'9ABC'DEF0ULL, 0x0BAD'C0DE'F00D'FACEULL};

static_assert(reps2::runtime::parallel_compare_greater_word(kLhs, kRhs).low == kExpected.low);
static_assert(reps2::runtime::parallel_compare_greater_word(kLhs, kRhs).high == kExpected.high);
static_assert(reps2::runtime::parallel_compare_greater_word(kLhs, kLhs).low == kZero.low);
static_assert(reps2::runtime::parallel_compare_greater_word(kLhs, kLhs).high == kZero.high);

} // namespace

int main() {
    EeState state{};
    state.gpr[0] = kSentinel;
    state.gpr[1] = kLhs;
    state.gpr[2] = kRhs;
    state.gpr[3] = kSentinel;
    state.gpr[4] = kSentinel;
    state.gpr[5] = kLhs;
    state.gpr[6] = kRhs;
    state.gpr[7] = kLhs;
    state.gpr[20] = kSentinel;
    if (reps2::generated::execute(state) != Status::ok) return 1;
    if (state.gpr[3].low != kExpected.low || state.gpr[3].high != kExpected.high) return 2;
    if (state.gpr[4].low != kExpected.low || state.gpr[4].high != kExpected.high) return 3;
    if (state.gpr[1].low != kExpected.low || state.gpr[1].high != kExpected.high) return 4;
    if (state.gpr[6].low != kExpected.low || state.gpr[6].high != kExpected.high) return 5;
    if (state.gpr[7].low != kZero.low || state.gpr[7].high != kZero.high) return 6;
    if (state.gpr[0].low != 0U || state.gpr[0].high != 0U) return 7;
    if (state.gpr[2].low != kRhs.low || state.gpr[2].high != kRhs.high) return 8;
    if (state.gpr[5].low != kLhs.low || state.gpr[5].high != kLhs.high) return 9;
    if (state.gpr[20].low != kSentinel.low || state.gpr[20].high != kSentinel.high) return 10;
    return 0;
}
