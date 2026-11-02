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
    0x1111'2222'3333'4444ULL,
    0xAAAA'BBBB'CCCC'DDDDULL,
};
constexpr Gpr128 kRhs{
    0x5555'6666'7777'8888ULL,
    0xEEEE'FFFF'0000'9999ULL,
};

// PCPYLD places rt.low in the low half and rs.low in the high half.
constexpr Gpr128 kExpected{
    0x5555'6666'7777'8888ULL,
    0x1111'2222'3333'4444ULL,
};
constexpr Gpr128 kSelfExpected{
    0x1111'2222'3333'4444ULL,
    0x1111'2222'3333'4444ULL,
};

static_assert(reps2::runtime::parallel_copy_low_doubleword(kLhs, kRhs).low == kExpected.low);
static_assert(reps2::runtime::parallel_copy_low_doubleword(kLhs, kRhs).high == kExpected.high);
static_assert(reps2::runtime::parallel_copy_low_doubleword(kLhs, kLhs).low == kSelfExpected.low);
static_assert(reps2::runtime::parallel_copy_low_doubleword(kLhs, kLhs).high == kSelfExpected.high);

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
    state.gpr[10] = kLhs;

    if (reps2::generated::execute(state) != Status::ok) return 1;

    // PCPYLD r3, r1, r2.
    if (state.gpr[3].low != kExpected.low) return 2;
    if (state.gpr[3].high != kExpected.high) return 3;

    // Destination aliases the first source: the original rs.low must still
    // reach the high half after the low half is overwritten.
    if (state.gpr[5].low != kExpected.low) return 4;
    if (state.gpr[5].high != kExpected.high) return 5;

    // Destination aliases the second source: the original rt.low must still
    // reach the low half after the high half is overwritten.
    if (state.gpr[7].low != kExpected.low) return 6;
    if (state.gpr[7].high != kExpected.high) return 7;

    // Destination aliases both sources: both halves receive the original low
    // doubleword.
    if (state.gpr[10].low != kSelfExpected.low) return 8;
    if (state.gpr[10].high != kSelfExpected.high) return 9;

    // The generated guard suppresses writes to the architecturally-zero GPR.
    if (state.gpr[0].low != 0U || state.gpr[0].high != 0U) return 10;

    return 0;
}
