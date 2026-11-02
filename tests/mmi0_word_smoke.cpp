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

constexpr Gpr128 kLhs{
    0x7FFF'FFFF'FFFF'FFFFULL, // lane 1 = 0x7FFFFFFF, lane 0 = 0xFFFFFFFF
    0x0000'0000'8000'0000ULL, // lane 3 = 0, lane 2 = 0x80000000
};
constexpr Gpr128 kRhs{
    0xFFFF'FFFF'0000'0001ULL, // lane 1 = 0xFFFFFFFF, lane 0 = 1
    0x8000'0000'FFFF'FFFFULL, // lane 3 = 0x80000000, lane 2 = 0xFFFFFFFF
};

constexpr Gpr128 kAddExpected{
    0x7FFF'FFFE'0000'0000ULL,
    0x8000'0000'7FFF'FFFFULL,
};
constexpr Gpr128 kSubExpected{
    0x8000'0000'FFFF'FFFEULL,
    0x8000'0000'8000'0001ULL,
};

constexpr Gpr128 kHalfAddExpected{
    0x7FFE'FFFE'FFFF'0000ULL,
    0x8000'0000'7FFF'FFFFULL,
};
constexpr Gpr128 kHalfSubExpected{
    0x8000'0000'FFFF'FFFEULL,
    0x8000'0000'8001'0001ULL,
};

static_assert(reps2::runtime::parallel_add_word(kLhs, kRhs).low == kAddExpected.low);
static_assert(reps2::runtime::parallel_add_word(kLhs, kRhs).high == kAddExpected.high);
static_assert(reps2::runtime::parallel_sub_word(kLhs, kRhs).low == kSubExpected.low);
static_assert(reps2::runtime::parallel_sub_word(kLhs, kRhs).high == kSubExpected.high);
static_assert(reps2::runtime::parallel_add_halfword(kLhs, kRhs).low == kHalfAddExpected.low);
static_assert(reps2::runtime::parallel_add_halfword(kLhs, kRhs).high == kHalfAddExpected.high);
static_assert(reps2::runtime::parallel_sub_halfword(kLhs, kRhs).low == kHalfSubExpected.low);
static_assert(reps2::runtime::parallel_sub_halfword(kLhs, kRhs).high == kHalfSubExpected.high);

} // namespace

int main() {
    EeState state{};
    state.gpr[0] = Gpr128{0xDEAD'BEEF'CAFE'BABEULL, 0xBAD0'BAD0'BAD0'BAD0ULL};
    state.gpr[1] = kLhs;
    state.gpr[2] = kRhs;
    state.gpr[5] = kLhs;
    state.gpr[6] = kRhs;
    state.gpr[8] = kLhs;
    state.gpr[7] = kRhs;
    state.gpr[11] = kLhs;
    state.gpr[12] = kRhs;
    state.gpr[14] = kLhs;
    state.gpr[13] = kRhs;

    if (reps2::generated::execute(state) != Status::ok) return 1;

    // PADDW and PSUBW wrap each of the four 32-bit lanes independently.
    if (state.gpr[3].low != kAddExpected.low) return 2;
    if (state.gpr[3].high != kAddExpected.high) return 3;
    if (state.gpr[4].low != kSubExpected.low) return 4;
    if (state.gpr[4].high != kSubExpected.high) return 5;

    // Destination may alias either source; no lane may observe a partial write.
    if (state.gpr[5].low != kAddExpected.low) return 6;
    if (state.gpr[5].high != kAddExpected.high) return 7;
    if (state.gpr[7].low != kSubExpected.low) return 8;
    if (state.gpr[7].high != kSubExpected.high) return 9;

    // The generated guard suppresses writes to the architecturally-zero GPR.
    if (state.gpr[0].low != 0U || state.gpr[0].high != 0U) return 10;

    // PADDH and PSUBH wrap each of the eight 16-bit lanes independently.
    if (state.gpr[9].low != kHalfAddExpected.low) return 11;
    if (state.gpr[9].high != kHalfAddExpected.high) return 12;
    if (state.gpr[10].low != kHalfSubExpected.low) return 13;
    if (state.gpr[10].high != kHalfSubExpected.high) return 14;

    // Halfword destination aliasing must also preserve the complete result.
    if (state.gpr[11].low != kHalfAddExpected.low) return 15;
    if (state.gpr[11].high != kHalfAddExpected.high) return 16;
    if (state.gpr[13].low != kHalfSubExpected.low) return 17;
    if (state.gpr[13].high != kHalfSubExpected.high) return 18;

    return 0;
}
