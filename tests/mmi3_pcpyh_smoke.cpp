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

constexpr Gpr128 kSrc{
    0x1111'2222'3333'4444ULL,
    0xAAAA'BBBB'CCCC'DDDDULL,
};

// PCPYH replicates the lowest halfword of each 64-bit half across that half.
constexpr Gpr128 kExpected{
    0x4444'4444'4444'4444ULL,
    0xDDDD'DDDD'DDDD'DDDDULL,
};

constexpr Gpr128 kSrc2{
    0x0123'4567'89AB'CDEFULL,
    0xFEDC'BA98'7654'3210ULL,
};

constexpr Gpr128 kExpected2{
    0xCDEF'CDEF'CDEF'CDEFULL,
    0x3210'3210'3210'3210ULL,
};

constexpr Gpr128 kSentinel{0x1234'5678'9ABC'DEF0ULL, 0x0BAD'C0DE'F00D'FACEULL};

static_assert(reps2::runtime::parallel_copy_halfword(kSrc).low == kExpected.low);
static_assert(reps2::runtime::parallel_copy_halfword(kSrc).high == kExpected.high);
static_assert(reps2::runtime::parallel_copy_halfword(kSrc2).low == kExpected2.low);
static_assert(reps2::runtime::parallel_copy_halfword(kSrc2).high == kExpected2.high);

} // namespace

int main() {
    EeState state{};
    state.gpr[0] = Gpr128{0xDEAD'BEEF'CAFE'BABEULL, 0xBAD0'BAD0'BAD0'BAD0ULL};
    state.gpr[2] = kSrc;
    state.gpr[3] = kSentinel;
    state.gpr[4] = kSentinel;
    state.gpr[5] = kSrc2;
    state.gpr[6] = kSrc2;
    state.gpr[20] = kSentinel;
    if (reps2::generated::execute(state) != Status::ok) return 1;
    if (state.gpr[3].low != kExpected.low) return 2;
    if (state.gpr[3].high != kExpected.high) return 3;
    if (state.gpr[2].low != kSrc.low || state.gpr[2].high != kSrc.high) return 4;
    if (state.gpr[0].low != 0U || state.gpr[0].high != 0U) return 5;
    if (state.gpr[20].low != kSentinel.low || state.gpr[20].high != kSentinel.high) return 6;
    if (state.gpr[4].low != kExpected2.low) return 7;
    if (state.gpr[4].high != kExpected2.high) return 8;
    if (state.gpr[5].low != kSrc2.low || state.gpr[5].high != kSrc2.high) return 9;
    if (state.gpr[6].low != kExpected2.low) return 10;
    if (state.gpr[6].high != kExpected2.high) return 11;
    return 0;
}